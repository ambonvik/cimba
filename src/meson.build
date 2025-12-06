# src/meson.build

# Platform independent library source code
sources = files('cimba.c',
                'cmb_assert.c',
                'cmb_buffer.c',
                'cmb_condition.c',
                'cmb_dataset.c',
                'cmb_datasummary.c',
                'cmb_event.c',
                'cmb_holdable.c',
                'cmb_logger.c',
                'cmb_objectqueue.c',
                'cmb_process.c',
                'cmb_random.c',
                'cmb_resource.c',
                'cmb_resourcebase.c',
                'cmb_resourceguard.c',
                'cmb_resourcestore.c',
                'cmb_timeseries.c',
                'cmb_wtdsummary.c',
                'cmi_coroutine.c',
                'cmi_hashheap.c',
                'cmi_mempool.c'
)

inc_int = include_directories('.')

# Platform and operating system dependent source code
subdir('port')

cimba_lib = library('cimba',
                    sources,
                    include_directories : [inc_api, inc_int],
                    c_args : ['-Wno-pedantic'],
                    link_args : ['-lm'],
                    dependencies : [project_deps, codegen_deps],
                    install : false,
                    version : meson.project_version(),
                    soversion : '1',
                    native : true
)

my_lib_dep = declare_dependency(
                    link_with : cimba_lib,
                    include_directories : [inc_api, inc_int]
)
