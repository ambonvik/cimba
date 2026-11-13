# port/meson.build

if host_machine.cpu_family() == 'x86_64'
    subdir('x86-64')
elif host_machine.cpu_family() == 'aarch64' and host_machine.system() == 'darwin'
    subdir('aarch64/macos')
elif host_machine.cpu_family() == 'aarch64' and host_machine.system() == 'linux'
    subdir('aarch64/linux')
else
    error('Unsupported platform: ' + host_machine.system() + '/' + host_machine.cpu_family())
endif
