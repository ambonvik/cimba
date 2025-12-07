# port/x86-64/meson.build

if host_machine.system() == 'linux'
    subdir('linux')
elif host_machine.system() == 'windows'
    subdir('windows')
else
    error('Unsupported platform: ' + host_machine.system())
endif
