# benchmark/meson.build

MM1_single = executable('MM1_single',
                         files('MM1_single.c'),
                         include_directories : [inc_api, inc_int],
                         link_with : cimba_lib,
                         dependencies : [project_deps],
                         install : false,
                         native : true
)

MM1_multi = executable('MM1_multi',
                        files('MM1_multi.c'),
                        include_directories : [inc_api, inc_int],
                        link_with : cimba_lib,
                        dependencies : [project_deps],
                        install : false,
                        native : true
)
