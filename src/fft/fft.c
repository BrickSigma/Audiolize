/* fft.c
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

#include <fft/fft.h>

#include <fftw3.h>
#include <portaudio-common/pa_ringbuffer.h>
#include <audio-driver/audio-driver.h>
#include <math.h>

// Number of frequency bins to output.
#define FREQUENCIES (7)

// Nyquist bin value (should be half of the sampling frame)
#define NYQUIST_BIN (256)

const unsigned int FREQUENCY_RANGES[FREQUENCIES] = {
    60, 150, 400, 1000, 2400, 6000, 14000};

/**
 * Struct used to handle the fourier transform thread and data.
 */
struct _AudiolizeFFT
{
    GObject parent_instance;

    // Cancellable used for closing the thread.
    GCancellable *canellable;

    // Sample rate of audio data
    guint sample_rate;

    // Ring buffer reference for input audio data
    PaUtilRingBuffer *audio_rb;

    // Holds the element being read from the input ring buffer
    AudioData *input_data;

    // FFT output array
    fftw_complex *out;
    // FFTW plan
    fftw_plan fftw_plan;
    // Array of samples to input to FFTW
    double *samples;

    // Ring buffer used for rendering output
    PaUtilRingBuffer *out_rb;
    // Data array for output ring buffer
    double *rb_data;

    // Cairo surface for drawing to
    cairo_surface_t *surface;

    // Weak reference to GtkDrawingArea to send draw update signal to
    GtkDrawingArea *drawing_area;
};

G_DEFINE_FINAL_TYPE(AudiolizeFFT, audiolize_fft, G_TYPE_OBJECT)

// Render the FFT bar graph to the surface. This must run on the main UI thread.
static gboolean audiolize_fft_render_fft(gpointer user_data);

static void
audiolize_fft_thread_cb(GTask *task,
                        gpointer source_object,
                        gpointer task_data,
                        GCancellable *cancellable)
{
    AudiolizeFFT *self;
    ring_buffer_size_t elements_read, elements_written;

    // This is used for converting the desired frequency to the set bin index
    double fs_n;

    double output[FREQUENCIES];
    // Holds the computed amplitude of each K-bin (frequency bin)
    double mapped_samples[NYQUIST_BIN];

    self = (AudiolizeFFT *)source_object;
    fs_n = ((double)FRAMES_PER_BUFFER / (double)(self->sample_rate));

    while (true)
    {
        int last_frequency;
        int low_bin;
        int high_bin;
        double max_amplitude;

        if (g_cancellable_is_cancelled(self->canellable))
            break;

        elements_read = PaUtil_ReadRingBuffer(self->audio_rb, self->input_data, 1);
        if (elements_read == 0)
            continue;

        // Get the average of the left and right samples
        for (int i = 0; i < FRAMES_PER_BUFFER; i++)
        {
            double sample = (double)(self->input_data[i * 2] + self->input_data[i * 2 + 1]);
            sample /= 2;
            self->samples[i] = sample;
        }

        // Execute the fourier transform on the input data
        fftw_execute(self->fftw_plan);

        // Compute the amplitude of the FFT output
        for (int i = 0; i < NYQUIST_BIN; i++)
        {
            mapped_samples[i] =
                sqrt((self->out[i][0] * self->out[i][0]) +
                     (self->out[i][1] * self->out[i][1])) /
                FRAMES_PER_BUFFER;
        }

        last_frequency = FREQUENCY_RANGES[0];
        for (int i = 1; i < FREQUENCIES; i++)
        {
            low_bin = (int)floor(last_frequency * fs_n);
            high_bin = (int)floor(FREQUENCY_RANGES[i] * fs_n);

            max_amplitude = 0;
            for (int j = low_bin + 1; j <= high_bin; j++)
            {
                if (mapped_samples[j] > max_amplitude)
                    max_amplitude = mapped_samples[j];
            }
            output[i - 1] = max_amplitude;

            last_frequency = FREQUENCY_RANGES[i];
        }

        low_bin = (int)floor(last_frequency * fs_n);
        high_bin = NYQUIST_BIN;

        max_amplitude = 0;
        for (int j = low_bin + 1; j <= high_bin; j++)
        {
            if (mapped_samples[j] > max_amplitude)
                max_amplitude = mapped_samples[j];
        }
        output[FREQUENCIES - 1] = max_amplitude;

        // g_print("[");
        // for (int i = 0; i < FREQUENCIES; i++)
        // {
        //     g_print("%lf,", output[i]);
        // }
        // g_print("]\n");

        // Send the output data to the ring buffer
        elements_written = PaUtil_WriteRingBuffer(self->out_rb, output, 1);

        if (elements_written == 1)
            g_main_context_invoke(g_main_context_get_thread_default(),
                                  audiolize_fft_render_fft, self);
    }
}

static void
audiolize_fft_finished_cb(GObject *source_object,
                          GAsyncResult *result,
                          gpointer data)
{
    g_print("Thread closed\n");
}

static void
audiolize_fft_init(AudiolizeFFT *self)
{
}

// Clear the surface and fill it with a color.
static void
audiolize_fft_clear_surface(AudiolizeFFT *self)
{
    cairo_t *cr;

    cr = cairo_create(self->surface);
    cairo_set_source_rgba(cr, 1, 1, 1, 1);
    cairo_paint(cr);
    cairo_destroy(cr);
}

void audiolize_fft_resize_surface(AudiolizeFFT *self,
                                  gint width,
                                  gint height)
{
    if (self->surface != NULL)
    {
        cairo_surface_destroy(self->surface);
        self->surface = NULL;
    }

    // Setup the Cairo surface for rendering
    self->surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
                                               width,
                                               height);

    audiolize_fft_clear_surface(self);
}

static gboolean
audiolize_fft_render_fft(gpointer user_data)
{
    AudiolizeFFT *self;
    double fft_output[FREQUENCIES];
    ring_buffer_size_t elements_read;

    self = user_data;

    elements_read = PaUtil_ReadRingBuffer(self->out_rb, fft_output, 1);
    if (elements_read == 0)
        return G_SOURCE_REMOVE;

    audiolize_fft_clear_surface(self);

    // Only call the draw queue is the drawing area still exists
    if (self->drawing_area != NULL)
        gtk_widget_queue_draw(GTK_WIDGET(self->drawing_area));

    return G_SOURCE_REMOVE;
}

void audiolize_fft_paint_surface(AudiolizeFFT *self,
                                 cairo_t *cr,
                                 int width,
                                 int height)
{
    cairo_set_source_surface(cr, self->surface, 0, 0);
    cairo_paint(cr);
}

// Used to set the drawing area to NULL on destruction.
// This is mainly needed if the app is being closed but the fft thread is still running.
static void
unref_drawing_area(gpointer data,
                   GObject *disposed_object)
{
    AudiolizeFFT *self = data;

    self->drawing_area = NULL;
}

/**
 * Setup the FFT object fully: this involves allocating memory and starting the FFT thread.
 *
 * @note This MUST be called immediately after the object is created in the `audiolize_fft_new` function.
 */
static void
audiolize_fft_setup(AudiolizeFFT *self, guint sample_rate, gpointer audio_rb, GtkDrawingArea *drawing_area)
{
    ring_buffer_size_t rb_size;
    GTask *task;

    self->sample_rate = sample_rate;
    self->audio_rb = audio_rb;

    // Setup output ring buffer
    self->rb_data = (double *)g_malloc(sizeof(double) * FREQUENCIES * RING_BUFFER_SIZE);
    self->out_rb = (PaUtilRingBuffer *)g_new0(PaUtilRingBuffer, 1);
    rb_size = PaUtil_InitializeRingBuffer(self->out_rb,
                                          sizeof(double) * FREQUENCIES,
                                          RING_BUFFER_SIZE,
                                          self->rb_data);

    if (rb_size < 0)
    {
        fprintf(stderr, "ERROR: Could not initialize output ring buffer!\n");
        g_free(self->rb_data);
        g_free(self->out_rb);
        return;
    }

    // Allocate memory for the input data from the ringn buffer
    self->input_data = (AudioData *)g_malloc(AUDIO_FRAME_SIZE);

    // Setup allocations for FFTW
    self->samples = (double *)fftw_malloc(sizeof(double) * FRAMES_PER_BUFFER);
    self->out = (fftw_complex *)fftw_malloc(sizeof(fftw_complex) * FRAMES_PER_BUFFER);
    self->fftw_plan = fftw_plan_dft_r2c_1d(FRAMES_PER_BUFFER, self->samples, self->out, FFTW_MEASURE);

    // Set the drawing surface to NULL and create a weak reference to the drawing area
    self->surface = NULL;
    g_object_weak_ref(G_OBJECT(drawing_area), unref_drawing_area, self);
    self->drawing_area = drawing_area;

    // Start the thread for handling the audio data
    self->canellable = g_cancellable_new();
    task = g_task_new(self, self->canellable, audiolize_fft_finished_cb, NULL);
    // g_task_set_return_on_cancel(task, true); // Ensure the thread closes when cancelled

    g_task_run_in_thread(task, audiolize_fft_thread_cb);
    g_object_unref(task);
}

static void
audiolize_fft_finalize(GObject *gobject)
{
    AudiolizeFFT *self;

    g_print("Closing FFT object\n");

    self = AUDIOLIZE_FFT(gobject);
    g_object_unref(self->canellable);

    if (self->surface != NULL)
    {
        cairo_surface_destroy(self->surface);
        self->surface = NULL;
    }

    fftw_destroy_plan(self->fftw_plan);
    fftw_free(self->out);
    fftw_free(self->samples);

    g_free(self->input_data);

    g_free(self->rb_data);
    g_free(self->out_rb);

    G_OBJECT_CLASS(audiolize_fft_parent_class)->finalize(gobject);
}

static void
audiolize_fft_class_init(AudiolizeFFTClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);

    object_class->finalize = audiolize_fft_finalize;

    // Ensure the nyquist frequency is exactly half of the frame per buffer
    g_assert(FRAMES_PER_BUFFER == (NYQUIST_BIN * 2));
}

void audiolize_fft_cancel_task(AudiolizeFFT *self)
{
    g_cancellable_cancel(self->canellable);
}

AudiolizeFFT *audiolize_fft_new(guint sample_rate, gpointer audio_rb, GtkDrawingArea *drawing_area)
{
    AudiolizeFFT *fft = AUDIOLIZE_FFT(g_object_new(AUDIOLIZE_TYPE_FFT,
                                                   NULL));

    audiolize_fft_setup(fft, sample_rate, audio_rb, drawing_area);

    return fft;
}