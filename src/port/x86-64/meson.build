# port/x86-64/meson.build

if host_machine.system() == 'linux'
    subdir('Linux')
elif host_machine.system() == 'windows'
    subdir('Windows')
else
    error('Unsupported platform: ' + host_machine.system())
endif
