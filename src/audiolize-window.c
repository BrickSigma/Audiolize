/* audiolize-window.c
 *
 * Copyright 2025 Junaid Chaudhry
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */

#include "config.h"

#include "audiolize-window.h"
#include <portaudio.h>
#include <portaudio-common/pa_ringbuffer.h>

#define FRAMES_PER_BUFFER (256)
#define CHANNELS (2)

// Size of input audio frame
#define AUDIO_FRAME_SIZE (sizeof(float) * FRAMES_PER_BUFFER * CHANNELS)

#define RING_BUFFER_SIZE (4)

struct _AudiolizeWindow
{
	AdwApplicationWindow parent_instance;

	/* Template widgets */

	GtkLabel *label;
	GtkDropDown *devices_list;

	/* PortAudio stuff */
	// List of connected devices
	PaDeviceInfo **devices;
	// Currently selected device
	PaDeviceInfo *selected_device;
	// Input stream
	PaStream *stream;
	// Audio data array for ring buffer
	float *audio_data;
	// Ring buffer for audio data
	PaUtilRingBuffer *ring_buffer;
};

G_DEFINE_FINAL_TYPE(AudiolizeWindow, audiolize_window, ADW_TYPE_APPLICATION_WINDOW)

static void close_input_stream(AudiolizeWindow *self);
static void open_input_stream(AudiolizeWindow *self, guint selected_device_index, PaDeviceInfo *selected_device);

static void
audiolize_window_dispose(GObject *gobject)
{
	gtk_widget_dispose_template(GTK_WIDGET(gobject), AUDIOLIZE_TYPE_WINDOW);

	G_OBJECT_CLASS(audiolize_window_parent_class)->dispose(gobject);
}

// Destructor to clear any allocated memory.
static void
audiolize_window_finalize(GObject *gobject)
{
	PaError err;
	AudiolizeWindow *self = AUDIOLIZE_WINDOW(gobject);

	// Close the input stream
	close_input_stream(self);

	// Release allocated resources
	g_free(self->devices);
	g_free(self->audio_data);
	g_free(self->ring_buffer);

	err = Pa_Terminate();
	if (err != paNoError)
		fprintf(stderr, "ERROR: Could not initialize PortAudio: %s\n", Pa_GetErrorText(err));

	G_OBJECT_CLASS(audiolize_window_parent_class)->finalize(gobject);
}

static void
audiolize_window_class_init(AudiolizeWindowClass *klass)
{
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);

	G_OBJECT_CLASS(klass)->dispose = audiolize_window_dispose;
	G_OBJECT_CLASS(klass)->finalize = audiolize_window_finalize;

	gtk_widget_class_set_template_from_resource(widget_class, "/io/bricksigma/Audiolize/audiolize-window.ui");
	gtk_widget_class_bind_template_child(widget_class, AudiolizeWindow, label);
	gtk_widget_class_bind_template_child(widget_class, AudiolizeWindow, devices_list);
}

static void
device_factory_setup_cb(GtkSignalListItemFactory *self,
						GtkListItem *listitem,
						gpointer user_data)
{
	GtkWidget *label;

	label = gtk_label_new("");

	gtk_label_set_max_width_chars(GTK_LABEL(label), 24);
	gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
	gtk_label_set_xalign(GTK_LABEL(label), 0);

	gtk_list_item_set_child(listitem, label);
}

static void
device_popup_factory_setup_cb(GtkSignalListItemFactory *self,
							  GtkListItem *listitem,
							  gpointer user_data)
{
	GtkWidget *label;

	label = gtk_label_new("");
	gtk_label_set_xalign(GTK_LABEL(label), 0);

	gtk_list_item_set_child(listitem, label);
}

static void
device_factory_bind_cb(GtkSignalListItemFactory *self,
					   GtkListItem *listitem,
					   gpointer user_data)
{
	GtkWidget *label;
	GtkStringObject *name;

	label = gtk_list_item_get_child(listitem);
	name = gtk_list_item_get_item(listitem);

	gtk_label_set_text(GTK_LABEL(label), gtk_string_object_get_string(name));
}

// Initialize the list of portaudio devices connected.
static void
initialize_device_list(AudiolizeWindow *self)
{
	PaError err;
	int num_devices;
	GtkStringList *device_names;
	GtkListItemFactory *factory;

	// Setup portaudio
	err = Pa_Initialize();
	if (err != paNoError)
		fprintf(stderr, "ERROR: Could not initialize PortAudio: %s\n", Pa_GetErrorText(err));

	g_print("PortAudio initialized!\n");

	// Get the devices connected
	num_devices = Pa_GetDeviceCount();
	if (num_devices <= 0)
	{
		err = num_devices;
		fprintf(stderr, "ERROR: No devices connected: %s\n", Pa_GetErrorText(err));
	}

	// Setup the list model for the drop down menu
	device_names = gtk_string_list_new(NULL);
	self->devices = g_new(PaDeviceInfo *, num_devices);
	for (int i = 0; i < num_devices; i++)
	{
		self->devices[i] = (PaDeviceInfo *)Pa_GetDeviceInfo(i);
		g_print("%02d: %s\n", i, self->devices[i]->name);
		gtk_string_list_append(device_names, self->devices[i]->name);
	}

	// Add the string list model to the drop down
	gtk_drop_down_set_model(self->devices_list, G_LIST_MODEL(device_names));
	g_object_unref(device_names);

	// Let's also setup a custom list factory so that the text is cut short with ellipses
	factory = gtk_signal_list_item_factory_new();

	g_signal_connect(factory, "setup", G_CALLBACK(device_factory_setup_cb), NULL);
	g_signal_connect(factory, "bind", G_CALLBACK(device_factory_bind_cb), NULL);
	gtk_drop_down_set_factory(self->devices_list, factory);

	g_object_unref(factory);

	// Setup the popup factory as well
	factory = gtk_signal_list_item_factory_new();

	g_signal_connect(factory, "setup", G_CALLBACK(device_popup_factory_setup_cb), NULL);
	g_signal_connect(factory, "bind", G_CALLBACK(device_factory_bind_cb), NULL);
	gtk_drop_down_set_list_factory(self->devices_list, factory);

	g_object_unref(factory);
}

static int
input_stream_cb(const void *input_buffer,
				void *output_buffer,
				unsigned long frame_count,
				const PaStreamCallbackTimeInfo *time_info,
				PaStreamCallbackFlags status_flags,
				void *user_data)
{
	PaUtilRingBuffer *rb = (PaUtilRingBuffer *)user_data;
	const float *input = (const float *)input_buffer;
	ring_buffer_size_t elements_written;

	elements_written = PaUtil_WriteRingBuffer(rb, input, 1);

	printf("Elements written: %ld\n", elements_written);

	return 0;
}

// Used to connect the selected device to portaudio input stream.
static void
open_input_stream(AudiolizeWindow *self, guint selected_device_index, PaDeviceInfo *selected_device)
{
	PaStreamParameters input_parameters;
	PaError err;

	if (self->stream != NULL)
		close_input_stream(self); // Close the stream if one was open before

	input_parameters = (PaStreamParameters){
		.channelCount = 2,
		.device = selected_device_index,
		.hostApiSpecificStreamInfo = NULL,
		.sampleFormat = paFloat32,
		.suggestedLatency = selected_device->defaultLowInputLatency};

	err = Pa_OpenStream(&(self->stream),
						&input_parameters, NULL,
						selected_device->defaultSampleRate, FRAMES_PER_BUFFER,
						paNoFlag,
						input_stream_cb, self->ring_buffer);

	if (err != paNoError)
		fprintf(stderr, "ERROR: Could not open PortAudio input stream: %s\n", Pa_GetErrorText(err));

	// Once the stream is opened, we can not start it
	err = Pa_StartStream(self->stream);
	if (err != paNoError)
		fprintf(stderr, "ERROR: Could not start PortAudio input stream: %s\n", Pa_GetErrorText(err));
}

// Used to close the input stream for portaudio.
static void
close_input_stream(AudiolizeWindow *self)
{
	PaError err;

	if (self->stream == NULL)
		return;

	err = Pa_StopStream(self->stream);
	if (err != paNoError)
		fprintf(stderr, "ERROR: Could not stop PortAudio input stream: %s\n", Pa_GetErrorText(err));

	err = Pa_CloseStream(self->stream);
	if (err != paNoError)
		fprintf(stderr, "ERROR: Could not close PortAudio input stream: %s\n", Pa_GetErrorText(err));

	self->stream = NULL;
}

// Callback used to update the input stream device for portaudio.
static void
selected_device_changed_cb(GtkDropDown *drop_down,
						   GParamSpec *pspec,
						   AudiolizeWindow *win)
{
	guint selected = gtk_drop_down_get_selected(drop_down);
	win->selected_device = win->devices[selected];

	g_print("Device changed to: %s\n", win->devices[selected]->name);

	// Close the current stream and open a new one
	close_input_stream(win);
	open_input_stream(win, selected, win->selected_device);
}

// Initialize the window.
static void
audiolize_window_init(AudiolizeWindow *self)
{
	ring_buffer_size_t rb_size;

	gtk_widget_init_template(GTK_WIDGET(self));

	g_print("Allocating\n");
	self->audio_data = (float *)g_malloc(AUDIO_FRAME_SIZE * RING_BUFFER_SIZE);
	if (self->audio_data == NULL)
		perror("Could not allocate memory for audio data");
	g_print("Done allocating\n");

	// Setup the ring buffer
	self->ring_buffer = (PaUtilRingBuffer *)g_new0(PaUtilRingBuffer, 1);
	rb_size = PaUtil_InitializeRingBuffer(self->ring_buffer,
										  sizeof(float) * FRAMES_PER_BUFFER * CHANNELS,
										  RING_BUFFER_SIZE,
										  self->audio_data);

	if (rb_size < 0)
		fprintf(stderr, "ERROR: Could not initialize ring buffer!\n");

	// Initialize portaudio and the list of devices connected
	initialize_device_list(self);

	// Setup a callback for when the selected device is changed
	g_signal_connect_after(self->devices_list, "notify::selected", G_CALLBACK(selected_device_changed_cb), self);

	// Open the input stream
	self->stream = NULL;
	self->selected_device = self->devices[0];
	open_input_stream(self, 0, self->selected_device);
}
