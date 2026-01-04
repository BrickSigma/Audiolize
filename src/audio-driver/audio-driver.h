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

#ifndef AUDIO_DRIVER_H
#define AUDIO_DRIVER_H

#include <portaudio-common/pa_ringbuffer.h>
#include <portaudio.h>

#define FRAMES_PER_BUFFER (256)
#define CHANNELS (2)

typedef float AudioData;

// Size of input audio frame
#define AUDIO_FRAME_SIZE (sizeof(AudioData) * FRAMES_PER_BUFFER * CHANNELS)

#define RING_BUFFER_SIZE (4)

// Audio driver struct: used for handling sound input with PortAudio.
typedef struct _AudioDriver
{
    // List of connected devices
    PaDeviceInfo **devices;
    // Number of connected devices
    int num_devices;
    // Currently selected device pointer
    PaDeviceInfo *selected_device;
    // Index number of selected device
    PaDeviceIndex selected_index;
    // Input stream
    PaStream *stream;
    // Audio data array for ring buffer
    AudioData *audio_data;
    // Ring buffer for audio data
    PaUtilRingBuffer *ring_buffer;
} AudioDriver;

// Create a new audio driver and initialize it.
AudioDriver *audio_driver_new(void);

// Close the audio driver.
void audio_driver_close(AudioDriver **audio_driver);

// Select a device to use for the input stream.
void audio_driver_set_selected_device(AudioDriver *audio_driver, PaDeviceIndex device);

// Opens the input stream for the audio driver.
void audio_driver_open_stream(AudioDriver *audio_driver);

// Close the input stream for the audio driver.
void audio_driver_close_stream(AudioDriver *audio_driver);

#endif // AUDIO_DRIVER_H