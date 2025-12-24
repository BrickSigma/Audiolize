# Audiolize

A simple audio visualizer application written with GTK and C, using PortAudio for audio input and the FFTW library for frequency analysis.

## Building
In order to build Audiolize, you need to have the following dependencies installed in your system, preferably through your system package manager (like DNF, APT, or Mingw):
- GTK 4
- LibAdwaita
- PortAudio-2.0
- FFTW

The project uses the Meson build system, and you can either build it in GNOME builder or in the terminal using the following commands, assuming you are in the root of the project folder:
```bash
meson setup builddir
cd builddir
meson compile
./src/audiolize
```