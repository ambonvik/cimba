# main meson.build

project('cimba', 'c',
        version : '3.0.0',
        default_options : [
            'warning_level=3',
            'c_std=c17',
            'buildtype=release'
        ]
)

# We also need the portable Netwide Assembler for assembly code
add_languages('nasm', required : true, native : true)

# Check for compiler flags availability
# Comment out all flags from -O3 and down for debug mode.
# DNDEBUG removes debug asserts
# DNLOGINFO removes all internal Cimba logging
# DNASSERT removes all asserts including argument checking
# DNMXCSR turns off saving and restoring the MXCSR floating point
#         state / control register in all context switches.
cc = meson.get_compiler('c')
desired_flags = ['-Wno-pedantic',
                 '-D_POSIX_C_SOURCE=200809L',
                 '-fno-semantic-interposition',
                 '-fno-omit-frame-pointer',
                 '-O3',
                 '-DNDEBUG',
                 '-DNLOGINFO',
                 '-DNASSERT',
                 '-DNMXCSR'
]

if host_machine.system() == 'linux'
    desired_flags += ['-ftls-model=initial-exec',
                 '-flto=auto',
                 '-fuse-linker-plugin'
    ]
endif

if host_machine.system() == 'windows'
    desired_flags += ['-fno-plt']
endif

supported_flags = cc.get_supported_arguments(desired_flags)
add_project_arguments(supported_flags, language : 'c', native : true)

nasm_args = []
if '-DNMXCSR' in supported_flags
    nasm_args += '-DNMXCSR'
endif

# Need to link with the math library, linker flag -lm
math_dep = cc.find_library('m', required : true)
project_deps = [math_dep]

# Build Cimba
subdir('include')
subdir('codegen')
subdir('src')
subdir('test')
subdir('tutorial')
subdir('benchmark')

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
