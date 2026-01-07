# test/meson.build

test_buffer = executable('test_buffer',
                         files('test_buffer.c', 'test.h'),
                         include_directories : [inc_api, inc_int],
                         link_with : cimba_lib,
                         dependencies : [project_deps],
                         install : false,
                         native : true
)

test('buffer', test_buffer)

test_cimba = executable('test_cimba',
                         files('test_cimba.c', 'test.h'),
                         include_directories : [inc_api, inc_int],
                         link_with : cimba_lib,
                         dependencies : [project_deps],
                         install : false,
                         native : true
)

# Make sure this one gets all cores to itself
test('cimba', test_cimba, is_parallel : false, timeout : 0)

test_condition = executable('test_condition',
                            files('test_condition.c', 'test.h'),
                            include_directories : [inc_api, inc_int],
                            link_with : cimba_lib,
                            dependencies : [project_deps],
                            install : false,
                            native : true
)

test('condition', test_condition)

test_coroutine = executable('test_coroutine',
                            files('test_coroutine.c', 'test.h'),
                            include_directories : [inc_api, inc_int],
                            link_with : cimba_lib,
                            dependencies : [project_deps],
                            install : false,
                            native : true
)

test('coroutine', test_coroutine)

test_data = executable( 'test_data',
                        files('test_data.c', 'test.h'),
                        include_directories : [inc_api, inc_int],
                        link_with : cimba_lib,
                        dependencies : [project_deps],
                        install : false,
                        native : true
)

# may take a while
test('data', test_data, timeout : 0)

test_event = executable('test_event',
                        files('test_event.c', 'test.h'),
                        include_directories : [inc_api, inc_int],
                        link_with : cimba_lib,
                        dependencies : [project_deps],
                        install : false,
                        native : true
)

test('event', test_event)

test_hashheap = executable('test_hashheap',
                        files('test_hashheap.c', 'test.h'),
                        include_directories : [inc_api, inc_int],
                        link_with : cimba_lib,
                        dependencies : [project_deps],
                        install : false,
                        native : true
)

test('hashheap', test_hashheap)

test_logger = executable('test_logger',
                         files('test_logger.c', 'test.h'),
                         include_directories : [inc_api, inc_int],
                         link_with : cimba_lib,
                         dependencies : [project_deps],
                         install : false,
                         native : true
)

test('logger', test_logger)

test_mempool = executable('test_mempool',
                         files('test_mempool.c', 'test.h'),
                         include_directories : [inc_api, inc_int],
                         link_with : cimba_lib,
                         dependencies : [project_deps],
                         install : false,
                         native : true
)

test('mempool', test_mempool)

test_objectqueue = executable('test_objectqueue',
                              files('test_objectqueue.c', 'test.h'),
                              include_directories : [inc_api, inc_int],
                              link_with : cimba_lib,
                              dependencies : [project_deps],
                              install : false,
                              native : true
)

test('object_queue', test_objectqueue)

test_priorityqueue = executable('test_priorityqueue',
                              files('test_priorityqueue.c', 'test.h'),
                              include_directories : [inc_api, inc_int],
                              link_with : cimba_lib,
                              dependencies : [project_deps],
                              install : false,
                              native : true
)

test('priority_queue', test_priorityqueue)

test_process = executable('test_process',
                            files('test_process.c', 'test.h'),
                            include_directories : [inc_api, inc_int],
                            link_with : cimba_lib,
                            dependencies : [project_deps],
                            install : false,
                            native : true
)

test('process', test_process)

test_random = executable('test_random',
                         files('test_random.c', 'test.h'),
                         include_directories : [inc_api, inc_int],
                         link_with : cimba_lib,
                         dependencies : [project_deps],
                         install : false,
                         native : true
)

# will certainly take a while
test('random', test_random, timeout : 0)

test_resource = executable('test_resource',
                          files('test_resource.c', 'test.h'),
                          include_directories : [inc_api, inc_int],
                          link_with : cimba_lib,
                          dependencies : [project_deps],
                          install : false,
                          native : true
)

test('resource', test_resource)

test_resourcepool = executable('test_resourcepool',
                           files('test_resourcepool.c', 'test.h'),
                           include_directories : [inc_api, inc_int],
                           link_with : cimba_lib,
                           dependencies : [project_deps],
                           install : false,
                           native : true
)

test('resourcepool', test_resourcepool)
