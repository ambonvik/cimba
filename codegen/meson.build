# codegen/meson.build

# Compile generator executables
genexp_exe = executable('calc_exponential', 'calc_exponential.c', native : true)
gennor_exe = executable('calc_normal', 'calc_normal.c', native : true)

# Define code generation step
exp_inc = custom_target('cmi_random_exp_zig.inc',
                        output : 'cmi_random_exp_zig.inc',
                        command : [genexp_exe],
                        capture : true
)

nor_inc = custom_target('cmi_random_nor_zig.inc',
                        output : 'cmi_random_nor_zig.inc',
                        command : [gennor_exe],
                        capture : true
)

# Expose these targets
codegen_deps = declare_dependency(sources : [exp_inc, nor_inc],
                                  include_directories : include_directories('.')
)

