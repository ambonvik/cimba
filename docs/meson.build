# docs/meson.build

doxygen = find_program('doxygen', required : false)
sphinx = find_program('sphinx-build', required : false)

if doxygen.found() and sphinx.found()
    cdata = configuration_data()
    cdata.set('PROJECT_NAME', meson.project_name())
    cdata.set('VERSION', meson.project_version())
    cdata.set('OUTPUT_DIR', join_paths(meson.project_build_root(), 'docs'))
    cdata.set('SOURCE_ROOT', meson.project_source_root())
    cdata.set('BUILD_ROOT', meson.project_build_root())

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
        input : ['index.rst',
                 'installation.rst',
                 'welcome.rst',
                 'tutorial.rst',
                 'background.rst',
                 'static/custom.css',
                 conf_py],
        output : 'html',
        command : [sphinx, '-b', 'html',
                    '-c', meson.current_build_dir(),
                    '-Dbreathe_projects.Cimba=' + join_paths(meson.project_build_root(), 'docs', 'xml'),
                    meson.current_source_dir(),
                    join_paths(meson.project_build_root(), 'docs', 'html')],
        depends : doxy_target,
        install : true,
        install_dir : join_paths(get_option('datadir'), 'doc', meson.project_name())
    )
else
    warning('Documentation tools not found. Skipping docs')
endif


