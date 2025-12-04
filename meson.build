# main meson.build

project('cimba', 'c',
        version : '3.0.0',
        default_options : ['warning_level=3', 'c_std=c17']
)

add_languages('nasm', required : true, native : true)

project_deps = []


subdir('codegen')
subdir('src')
subdir('test')

if get_option('enable_docs')
    subdir('docs')
endif
