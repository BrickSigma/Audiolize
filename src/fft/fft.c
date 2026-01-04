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

// Number of frequency bins to output
#define FREQUENCIES (6)

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
};

G_DEFINE_FINAL_TYPE(AudiolizeFFT, audiolize_fft, G_TYPE_OBJECT)

static void
fft_thread_cb(GTask *task,
              gpointer source_object,
              gpointer task_data,
              GCancellable *cancellable)
{
    static int x = 0;
    static int thread_id = 0;
    g_print("Thread started: %d\n", thread_id);
    thread_id++;
    while (true)
    {
        x++; // Used to prevent loop being optimized out (I think...)
    }
    g_print("%d\n", x);
}

static void
fft_finished_cb(GObject *source_object,
                GAsyncResult *result,
                gpointer data)
{
    g_print("Thread closed\n");
}

static void
audiolize_fft_init(AudiolizeFFT *self)
{
}

/**
 * Setup the FFT object fully: this involves allocating memory and starting the FFT thread.
 *
 * @note This MUST be called immediately after the object is created in the `audiolize_fft_new` function.
 */
static void
audiolize_fft_setup(AudiolizeFFT *self, guint sample_rate, gpointer audio_rb)
{
    ring_buffer_size_t rb_size;
    GTask *task;

    self->sample_rate = sample_rate;
    self->audio_rb = audio_rb;

    // Setup output ring buffer
    self->rb_data = (double *)g_malloc(sizeof(double) * FREQUENCIES * RING_BUFFER_SIZE);
    self->out_rb = (PaUtilRingBuffer *)g_new0(PaUtilRingBuffer, 1);
    rb_size = PaUtil_InitializeRingBuffer(self->out_rb,
                                          sizeof(AudioData) * FRAMES_PER_BUFFER * CHANNELS,
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

    // Start the thread for handling the audio data
    self->canellable = g_cancellable_new();
    task = g_task_new(self, self->canellable, fft_finished_cb, NULL);
    g_task_set_return_on_cancel(task, true);  // Ensure the thread closes when cancelled

    g_task_run_in_thread(task, fft_thread_cb);
    g_object_unref(task);
}

static void
audiolize_fft_finalize(GObject *gobject)
{
    AudiolizeFFT *self;

    g_print("Closing FFT object\n");

    self = AUDIOLIZE_FFT(gobject);
    g_cancellable_cancel(self->canellable);
    g_object_unref(self->canellable);

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
}

AudiolizeFFT *audiolize_fft_new(guint sample_rate, gpointer audio_rb)
{
    AudiolizeFFT *fft = AUDIOLIZE_FFT(g_object_new(AUDIOLIZE_TYPE_FFT,
                                                   NULL));

    audiolize_fft_setup(fft, sample_rate, audio_rb);

    return fft;
}