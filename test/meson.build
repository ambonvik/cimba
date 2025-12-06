# test/meson.build
test_data = executable('test_data',
                    files('test_data.c', 'test.h'),
                    include_directories : [inc_api, inc_int],
                    c_args : [],
                    link_with : cimba_lib,
                    link_args : ['-lm'],
                    dependencies : [project_deps, codegen_deps],
                    install : false,
                    native : true
)

test_event = executable('test_event',
                       files('test_event.c', 'test.h'),
                       include_directories : [inc_api, inc_int],
                       c_args : [],
                       link_with : cimba_lib,
                       link_args : ['-lm'],
                       dependencies : [project_deps, codegen_deps],
                       install : false,
                       native : true
)

test_hashheap = executable('test_hashheap',
                        files('test_hashheap.c', 'test.h'),
                        include_directories : [inc_api, inc_int],
                        c_args : [],
                        link_with : cimba_lib,
                        link_args : ['-lm'],
                        dependencies : [project_deps, codegen_deps],
                        install : false,
                        native : true
)

test_logger = executable('test_logger',
                           files('test_logger.c', 'test.h'),
                           include_directories : [inc_api, inc_int],
                           c_args : [],
                           link_with : cimba_lib,
                           link_args : ['-lm'],
                           dependencies : [project_deps, codegen_deps],
                           install : false,
                           native : true
)

test_mempool = executable('test_mempool',
                         files('test_mempool.c', 'test.h'),
                         include_directories : [inc_api, inc_int],
                         c_args : [],
                         link_with : cimba_lib,
                         link_args : ['-lm'],
                         dependencies : [project_deps, codegen_deps],
                         install : false,
                         native : true
)

test_random = executable('test_random',
                          files('test_random.c', 'test.h'),
                          include_directories : [inc_api, inc_int],
                          c_args : [],
                          link_with : cimba_lib,
                          link_args : ['-lm'],
                          dependencies : [project_deps, codegen_deps],
                          install : false,
                          native : true
)
