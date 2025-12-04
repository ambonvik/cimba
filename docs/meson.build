# docs/meson.build

doxygen = find_program('doxygen', required : false)
sphinx = find_program('sphinx-build', required : false)

if doxygen.found() and sphinx.found()
    cdata = configuration_data()
    cdata.set('PROJECT_NAME', meson.project_name())
    cdata.set('VERSION', meson.project_version())
    cdata.set('OUTPUT_DIR', join_paths(meson.project_build_root(), 'docs'))

    doxyfile = configure_file(
        input : 'Doxyfile.in',
        output : 'Doxyfile',
        configuration : cdata
    )

    conf_py = configure_file(
        input : 'conf.py.in',
        output : 'conf.py',
        configuration : cdata
    )

    doxy_target = custom_target('doxygen',
        input : doxyfile,
        output : 'xml',
        command : [doxygen, doxyfile],
        install : false
    )

    custom_target('sphinx',
        input : ['index.rst', conf_py],
        output : 'html',
        command : [sphinx, '-b', 'html',
                    '-Dbreathe_projects.cimba=' + join_paths(meson.project_build_root(), 'docs', 'xml'),
                    meson.current_source_dir(),
                    join_paths(meson.project_build_root(), 'docs', 'html')],
        depends : doxy_target,
        install : true,
        install_dir : join_paths(get_option('datadir'), 'doc', meson.project_name())
    )
else
    warning('Documentation tools not found. Skipping docs')
endif


