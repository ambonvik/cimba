# codegen/meson.build

# Compile generator executables
genexp_exe = executable('calc_exponential',
                        files('calc_exponential.c','calc_utils.c'),
                        link_args : ['-lm'],
                        native : true
)

gennor_exe = executable('calc_normal',
                        files('calc_normal.c', 'calc_utils.c'),
                        link_args : ['-lm'],
                        native : true
)

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
                                  include_directories : '.'
)

