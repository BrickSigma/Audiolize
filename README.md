# Audiolize
A simple audio visualizer application written with GTK and C, using PortAudio for audio input and the FFTW library for frequency analysis.


## Building
In order to build Audiolize, you need to have the following dependencies installed in your system, preferably through your system package manager (like DNF, APT, or Mingw):
- GTK 4
- LibAdwaita
- PortAudio-2.0
- FFTW
- ALSA-LIB (needed by PortAudio)

The project uses the Meson build system, and you can either build it in GNOME builder or in the terminal using the following commands, assuming you are in the root of the project folder:
```bash
meson setup builddir
cd builddir
meson compile
./src/audiolize
```

**Note for building with GNOME Builder:** Make sure you use the flatpak manifest file as the active configuration, this should allow you to build the application without needing to install the above libraries directly. If you use the default configuration, you'll need to install the above dependencies in order to build the project.