# tutorial/meson.build

tut_hello = executable('hello',
                         files('hello.c'),
                         include_directories : [inc_api, inc_int],
                         link_with : cimba_lib,
                         dependencies : [project_deps],
                         install : false,
                         native : true
)

tut_1_1 = executable('tut_1_1',
                       files('tut_1_1.c'),
                       include_directories : [inc_api, inc_int],
                       link_with : cimba_lib,
                       dependencies : [project_deps],
                       install : false,
                       native : true
)

tut_1_2 = executable('tut_1_2',
                     files('tut_1_2.c'),
                     include_directories : [inc_api, inc_int],
                     link_with : cimba_lib,
                     dependencies : [project_deps],
                     install : false,
                     native : true
)

tut_1_3 = executable('tut_1_3',
                     files('tut_1_3.c'),
                     include_directories : [inc_api, inc_int],
                     link_with : cimba_lib,
                     dependencies : [project_deps],
                     install : false,
                     native : true
)

tut_1_4 = executable('tut_1_4',
                     files('tut_1_4.c'),
                     include_directories : [inc_api, inc_int],
                     link_with : cimba_lib,
                     dependencies : [project_deps],
                     install : false,
                     native : true
)

tut_1_5 = executable('tut_1_5',
                     files('tut_1_5.c'),
                     include_directories : [inc_api, inc_int],
                     link_with : cimba_lib,
                     dependencies : [project_deps],
                     install : false,
                     native : true
)

tut_1_6 = executable('tut_1_6',
                     files('tut_1_6.c'),
                     include_directories : [inc_api, inc_int],
                     link_with : cimba_lib,
                     dependencies : [project_deps],
                     install : false,
                     native : true
)

tut_1_7 = executable('tut_1_7',
                     files('tut_1_7.c'),
                     include_directories : [inc_api, inc_int],
                     link_with : cimba_lib,
                     dependencies : [project_deps],
                     install : false,
                     native : true
)

tut_2_1 = executable('tut_2_1',
                     files('tut_2_1.c'),
                     include_directories : [inc_api, inc_int],
                     link_with : cimba_lib,
                     dependencies : [project_deps],
                     install : false,
                     native : true
)

tut_3_1 = executable('tut_3_1',
                     files('tut_3_1.c'),
                     include_directories : [inc_api, inc_int],
                     link_with : cimba_lib,
                     dependencies : [project_deps],
                     install : false,
                     native : true
)

tut_4_1 = executable('tut_4_1',
                     files('tut_4_1.c'),
                     include_directories : [inc_api, inc_int],
                     link_with : cimba_lib,
                     dependencies : [project_deps],
                     install : false,
                     native : true
)

tut_4_2 = executable('tut_4_2',
                     files('tut_4_2.c'),
                     include_directories : [inc_api, inc_int],
                     link_with : cimba_lib,
                     dependencies : [project_deps],
                     install : false,
                     native : true
)

# Try to find HDF5, but don't fail if it's missing
hdf5_dep = dependency('hdf5', required : false)

# Try to find CUDA, but don't fail if it's missing
cuda_dep = dependency('cuda', required : false, modules : ['cudart'])

if hdf5_dep.found()
    tut_5_1 = executable('tut_5_1',
                         files('tut_5_1.c'),
                         include_directories : [inc_api, inc_int],
                         link_with : cimba_lib,
                         dependencies : [project_deps, hdf5_dep],
                         link_args : ['-lhdf5'],
                         install : false,
                         native : true)
else
    message('Skipping tut_5_1: HDF5 not found.')
endif

if hdf5_dep.found() and cuda_dep.found()
    cuda_inc = include_directories('/opt/cuda/include')

    executable('tut_5_2',
               sources : ['tut_5_2.c', 'tut_5_2.cu'],
               include_directories : [inc_api, inc_int, cuda_inc],
               link_with : cimba_lib,
               dependencies : [project_deps, hdf5_dep, cuda_dep],
               c_args : ['-fno-lto'],
               link_args : ['-lhdf5', '-lcudart', '-lcurand'],
               install : false,
               native : true)
else
    if not hdf5_dep.found()
        message('Skipping tut_5_2: HDF5 not found.')
    endif
    if not cuda_dep.found()
        message('Skipping tut_5_2: CUDA not found.')
    endif
endif