# main meson.build

project('cimba', ['c', 'cuda'],
        version : '3.0.0',
        default_options : [
            'warning_level=3',
            'buildtype=release',
            'c_std=c23',
            'cuda_std=c++17',
        ]
)

# We also need the portable Netwide Assembler for assembly code
add_languages('nasm', required : true)

# We will also be calling CUDA on the GPU for tutorial 5 */
add_project_arguments(['-arch=sm_86', '-ccbin=/usr/bin/g++'], language : 'cuda')


# Check for compiler flags availability
# DNDEBUG removes debug asserts
# DNLOGINFO removes all internal Cimba logging
# DNASSERT removes all debug and release asserts (including argument checking)
# DNMXCSR turns off saving and restoring the MXCSR floating point
#         state / control register in all context switches.
cc = meson.get_compiler('c')
# Flags common to all build types
common_flags = [
    '-Wno-pedantic',
    '-D_POSIX_C_SOURCE=200809L',
    '-fno-semantic-interposition',
    '-DNMXCSR',
]

if cc.has_argument('-std=c23')
    common_flags += '-std=c23'
elif cc.has_argument('-std=c2x')
    common_flags += '-std=c2x'
else
    warning('Compiler does not support C23/C2x standard')
endif

# Flags per build type
# 'debug'    : full asserts and logging, no optimization
# 'release'  : standard optimized build, release asserts only
buildtype_flags = {
    'debug'   : ['-fno-omit-frame-pointer'],
    'release' : ['-DNDEBUG', '-DNLOGINFO'],
    'plain'   : [],
}

# No-holds-barred optimized build: meson setup --buildtype=release -Dbenchopt=true
benchopt_flags = [
    '-O3',
    '-DNDEBUG',
    '-DNLOGINFO',
    '-DNASSERT',
    '-fprofile-use',
    '-fprofile-correction',
]

benchopt_gen_flags = [
    '-O3',
    '-fprofile-generate',
    '-fno-omit-framepointer',
]

buildtype = get_option('buildtype')
desired_flags = common_flags + buildtype_flags.get(buildtype, [])

if get_option('benchopt') == 'generate'
    desired_flags += benchopt_gen_flags
elif get_option('benchopt') == 'use'
    desired_flags += benchopt_flags
endif

if host_machine.system() == 'linux'
    # The initial-exec model is faster than the default global-dynamic for TLS
    # access  and is safe as long as the library is loaded at program startup
    # (i.e. linked directly, not via dlopen at runtime).
    desired_flags += ['-ftls-model=initial-exec',
                      '-flto=auto',
                      '-fuse-linker-plugin'
    ]
endif

if host_machine.system() == 'windows'
    desired_flags += ['-fno-plt',
                      '-fcf-protection=none',
                      '-fno-stack-protector']
endif

supported_flags = cc.get_supported_arguments(desired_flags)
add_project_arguments(supported_flags, language : 'c', native : true)
if '-fprofile-generate' in supported_flags
    add_project_link_arguments('-fprofile-generate', language : 'c', native : true)
endif

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
