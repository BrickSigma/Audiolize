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
};

G_DEFINE_FINAL_TYPE(AudiolizeFFT, audiolize_fft, G_TYPE_OBJECT)

static void
fft_thread_cb(GTask *task,
              gpointer source_object,
              gpointer task_data,
              GCancellable *cancellable)
{
    AudiolizeFFT *self;
    ring_buffer_size_t elements_read;

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
        output[6] = max_amplitude;

        g_print("[");
        for (int i = 0; i < FREQUENCIES; i++)
        {
            g_print("%lf,", output[i]);
        }
        g_print("]\n");
    }
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

    // Start the thread for handling the audio data
    self->canellable = g_cancellable_new();
    task = g_task_new(self, self->canellable, fft_finished_cb, NULL);
    // g_task_set_return_on_cancel(task, true); // Ensure the thread closes when cancelled

    g_task_run_in_thread(task, fft_thread_cb);
    g_object_unref(task);
}

static void
audiolize_fft_finalize(GObject *gobject)
{
    AudiolizeFFT *self;

    g_print("Closing FFT object\n");

    self = AUDIOLIZE_FFT(gobject);
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

    // Ensure the nyquist frequency is exactly half of the frame per buffer
    g_assert(FRAMES_PER_BUFFER == (NYQUIST_BIN * 2));
}

void audiolize_fft_cancel_task(AudiolizeFFT *self)
{
    g_cancellable_cancel(self->canellable);
}

AudiolizeFFT *audiolize_fft_new(guint sample_rate, gpointer audio_rb)
{
    AudiolizeFFT *fft = AUDIOLIZE_FFT(g_object_new(AUDIOLIZE_TYPE_FFT,
                                                   NULL));

    audiolize_fft_setup(fft, sample_rate, audio_rb);

    return fft;
}