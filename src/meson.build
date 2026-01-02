audiolize_sources = [
  'main.c',
  'audiolize-application.c',
  'audiolize-window.c',
  'portaudio-common/pa_ringbuffer.c',
  'audio-driver/audio-driver.c'
]

audiolize_deps = [
  dependency('gtk4'),
  dependency('libadwaita-1', version: '>= 1.4'),
  dependency('portaudio-2.0')
]

audiolize_sources += gnome.compile_resources('audiolize-resources',
  'audiolize.gresource.xml',
  c_name: 'audiolize'
)

executable('audiolize', audiolize_sources,
  dependencies: audiolize_deps,
       install: true,
)
