/* fft.h
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

#ifndef FFT_H
#define FFT_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define AUDIOLIZE_TYPE_FFT (audiolize_fft_get_type())

G_DECLARE_FINAL_TYPE(AudiolizeFFT, audiolize_fft, AUDIOLIZE, FFT, GObject)

/**
 * Create a new FFT struct and start its thread.
 *
 * @param `sample_rate` sample rate of the audio input
 * @param `audio_rb` ring buffer pointer for the incomming audio data from portaudio
 * @param `drawing_area` drawing area widget
 */
AudiolizeFFT *audiolize_fft_new(guint sample_rate, gpointer audio_rb);

// Resizes the Cairo surface for rendering.
void audiolize_fft_resize_surface(AudiolizeFFT *self, gint width, gint height);

// Copies the surface to the drawing area. Doesn't actually update the surface.
void audiolize_fft_paint_surface(AudiolizeFFT *self, cairo_t *cr, int width, int height);

// Cancel the FFT thread.
void audiolize_fft_cancel_task(AudiolizeFFT *self);

G_END_DECLS

#endif // FFT_H