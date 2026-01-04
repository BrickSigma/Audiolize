/* audio-driver.h
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

#include <audio-driver/audio-driver.h>
#include <gtk/gtk.h>

/**
 * Create a new audio driver.
 *
 * This will initialize PortAudio, open a new input stream, setup the audio data ring buffer
 * and load the list of connected devices.
 */
AudioDriver *audio_driver_new(void)
{
    AudioDriver *audio_driver;

    ring_buffer_size_t rb_size;
    PaError err;

    audio_driver = (AudioDriver *)g_new0(AudioDriver, 1);

    // Initialize PortAudio
    err = Pa_Initialize();
    if (err != paNoError)
    {
        fprintf(stderr, "ERROR: Could not initialize PortAudio: %s\n", Pa_GetErrorText(err));
        goto audio_driver_new_error;
    }
    g_print("PortAudio initialized!\n");

    // Get the list of connected devices
    audio_driver->num_devices = Pa_GetDeviceCount();
    if (audio_driver->num_devices <= 0)
    {
        err = audio_driver->num_devices;
        fprintf(stderr, "ERROR: No devices connected: %s\n", Pa_GetErrorText(err));
        goto audio_driver_new_error;
    }

    audio_driver->devices = g_new(PaDeviceInfo *, audio_driver->num_devices);
    for (int i = 0; i < audio_driver->num_devices; i++)
    {
        audio_driver->devices[i] = (PaDeviceInfo *)Pa_GetDeviceInfo(i);
        g_print("%02d: %s\n", i, audio_driver->devices[i]->name);
    }

    // Setup the ring buffer
    audio_driver->audio_data = (AudioData *)g_malloc(AUDIO_FRAME_SIZE * RING_BUFFER_SIZE);

    audio_driver->ring_buffer = (PaUtilRingBuffer *)g_new0(PaUtilRingBuffer, 1);
    rb_size = PaUtil_InitializeRingBuffer(audio_driver->ring_buffer,
                                          AUDIO_FRAME_SIZE,
                                          RING_BUFFER_SIZE,
                                          audio_driver->audio_data);

    if (rb_size < 0)
    {
        fprintf(stderr, "ERROR: Could not initialize ring buffer!\n");
        goto audio_driver_new_error;
    }

    audio_driver->stream = NULL;
    audio_driver->selected_device = audio_driver->devices[0];
    audio_driver->selected_index = 0;

    return audio_driver;

audio_driver_new_error:
    g_free(audio_driver->ring_buffer);
    g_free(audio_driver->audio_data);
    g_free(audio_driver->devices);
    g_free(audio_driver);
    return NULL;
}

void audio_driver_close(AudioDriver **audio_driver)
{
    PaError err;

    // Close the input stream
    audio_driver_close_stream(*audio_driver);

    // Release allocated resources
    g_free((*audio_driver)->devices);
    g_free((*audio_driver)->audio_data);
    g_free((*audio_driver)->ring_buffer);
    g_free(*audio_driver);

    *audio_driver = NULL;

    err = Pa_Terminate();
    if (err != paNoError)
        fprintf(stderr, "ERROR: Could not initialize PortAudio: %s\n", Pa_GetErrorText(err));
}

void audio_driver_set_selected_device(AudioDriver *audio_driver, PaDeviceIndex device_index)
{
    if (device_index >= audio_driver->num_devices)
    {
        fprintf(stderr, "ERROR: Device index is out of range!\n");
        return;
    }

    audio_driver->selected_index = device_index;
    audio_driver->selected_device = audio_driver->devices[device_index];

    g_print("Device changed to: %s\n", audio_driver->selected_device->name);

    // Close the currently open stream
    audio_driver_close_stream(audio_driver);
    // Open a new stream with the newly selected device
    audio_driver_open_stream(audio_driver);
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

    PaUtil_WriteRingBuffer(rb, input, 1);

    return 0;
}

void audio_driver_open_stream(AudioDriver *audio_driver)
{
    PaStreamParameters input_parameters;
    PaError err;

    if (audio_driver->stream != NULL)
        audio_driver_close_stream(audio_driver); // Close the stream if one was open before

    input_parameters = (PaStreamParameters){
        .channelCount = 2,
        .device = audio_driver->selected_index,
        .hostApiSpecificStreamInfo = NULL,
        .sampleFormat = paFloat32,
        .suggestedLatency = audio_driver->selected_device->defaultLowInputLatency};

    err = Pa_OpenStream(&(audio_driver->stream),
                        &input_parameters, NULL,
                        audio_driver->selected_device->defaultSampleRate, FRAMES_PER_BUFFER,
                        paNoFlag,
                        input_stream_cb, audio_driver->ring_buffer);

    if (err != paNoError)
        fprintf(stderr, "ERROR: Could not open PortAudio input stream: %s\n", Pa_GetErrorText(err));

    // Once the stream is opened, we can not start it
    err = Pa_StartStream(audio_driver->stream);
    if (err != paNoError)
        fprintf(stderr, "ERROR: Could not start PortAudio input stream: %s\n", Pa_GetErrorText(err));
}

void audio_driver_close_stream(AudioDriver *audio_driver)
{
    PaError err;

    if (audio_driver->stream == NULL)
        return;

    err = Pa_StopStream(audio_driver->stream);
    if (err != paNoError)
        fprintf(stderr, "ERROR: Could not stop PortAudio input stream: %s\n", Pa_GetErrorText(err));

    err = Pa_CloseStream(audio_driver->stream);
    if (err != paNoError)
        fprintf(stderr, "ERROR: Could not close PortAudio input stream: %s\n", Pa_GetErrorText(err));

    audio_driver->stream = NULL;
}