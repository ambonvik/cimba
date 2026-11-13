# x86-64/macOS/meson.build
nasm_args += ['-f', 'macho64']

sources += files('cmb_random_hwseed.c',
                 'cmi_coroutine_context.c',
                 'cmi_cpu_cores.c',
                 'cmi_memutils.c'
)

sources += generator(find_program('nasm'),
                     output : '@BASENAME@.o',
                     arguments : nasm_args + ['@INPUT@', '-o', '@OUTPUT@']
).process('cmi_coroutine_context.asm')
