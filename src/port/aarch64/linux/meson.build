# aarch64/Linux/meson.build
# Page size, aligned allocation and core count are architecture independent,
# so reuse the x86-64 Linux versions rather than keeping a second copy.
sources += files('cmb_random_hwseed.c',
                 'cmi_coroutine_context.c',
                 'cmi_coroutine_context.S',
                 '../../x86-64/linux/cmi_cpu_cores.c',
                 '../../x86-64/linux/cmi_memutils.c'
)
