# main meson.build

project('cimba', 'c',
        version : '3.0.0',
        default_options : [
            'warning_level=3',
            'c_std=c17',
            'buildtype=release'
        ]
)

# Ensure system dependent argument format
cc = meson.get_compiler('c')
host_system = host_machine.system()

desired_flags = ['-Wno-pedantic', '-D_POSIX_C_SOURCE=200809L']

supported_flags = cc.get_supported_arguments(desired_flags)
add_project_arguments(supported_flags, language : 'c', native : true)

# We also need the portable Netwide Assembler
add_languages('nasm', required : true, native : true)

project_deps = []

# Build Cimba
subdir('include')
subdir('codegen')
subdir('src')
subdir('test')
subdir('tutorial')

pkg = import('pkgconfig')
pkg.generate(
    cimba_lib,
    name : 'cimba',
    description : 'Multithreaded discrete event simulation',
    url : 'https://github.com/ambonvik/cimba'
)

if get_option('enable_docs')
    subdir('docs')
endif
