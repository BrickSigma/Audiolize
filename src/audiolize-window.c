/* audiolize-window.c
 *
 * Copyright 2025 Junaid Chaudhry
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "config.h"

#include "audiolize-window.h"
#include <audio-driver/audio-driver.h>

struct _AudiolizeWindow
{
	AdwApplicationWindow parent_instance;

	/* Template widgets */

	GtkLabel *label;
	GtkDropDown *devices_list;

	AudioDriver *audio_driver;
};

G_DEFINE_FINAL_TYPE(AudiolizeWindow, audiolize_window, ADW_TYPE_APPLICATION_WINDOW)

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
	AudiolizeWindow *self = AUDIOLIZE_WINDOW(gobject);

	audio_driver_close(&(self->audio_driver));

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

// Initialize the list of portaudio devices connected in the UI
static void
initialize_device_list(AudiolizeWindow *self)
{
	int num_devices;
	GtkStringList *device_names;
	GtkListItemFactory *factory;

	num_devices = self->audio_driver->num_devices;

	// Setup the list model for the drop down menu
	device_names = gtk_string_list_new(NULL);
	for (int i = 0; i < num_devices; i++)
	{
		gtk_string_list_append(device_names, self->audio_driver->devices[i]->name);
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

// Callback used to update the input stream device for portaudio.
static void
selected_device_changed_cb(GtkDropDown *drop_down,
						   GParamSpec *pspec,
						   AudiolizeWindow *win)
{
	guint selected = gtk_drop_down_get_selected(drop_down);

	audio_driver_set_selected_device(win->audio_driver, selected);
}

// Initialize the window.
static void
audiolize_window_init(AudiolizeWindow *self)
{
	gtk_widget_init_template(GTK_WIDGET(self));

	// Initialize the audio driver
	self->audio_driver = audio_driver_new();
	if (self->audio_driver == NULL)
	{
		g_abort();
		return;
	}

	// Initialize the device list UI
	initialize_device_list(self);

	// Setup a callback for when the selected device is changed
	g_signal_connect_after(self->devices_list, "notify::selected", G_CALLBACK(selected_device_changed_cb), self);

	// Open the input stream
	audio_driver_open_stream(self->audio_driver);
}
