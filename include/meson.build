# include/meson.build

ver_data = configuration_data()
ver_data.set('CIMBA_VERSION', ver_full)
ver_data.set('CIMBA_VERSION_MAJOR', ver_core[0])
ver_data.set('CIMBA_VERSION_MINOR', ver_core[1])
ver_data.set('CIMBA_VERSION_PATCH', ver_core[2])
configure_file(input : 'cimba_version.h.in', output : 'cimba_version.h',
               configuration : ver_data,
               install : true, install_dir : get_option('includedir'))

inc_api = include_directories('.')

# Public API headers - installed to /usr/local/include
install_headers(
    'cimba.h',
    'cmb_assert.h',
    'cmb_buffer.h',
    'cmb_condition.h',
    'cmb_dataset.h',
    'cmb_datasummary.h',
    'cmb_event.h',
    'cmb_logger.h',
    'cmb_objectqueue.h',
    'cmb_priorityqueue.h',
    'cmb_process.h',
    'cmb_random.h',
    'cmb_resource.h',
    'cmb_resourceguard.h',
    'cmb_resourcepool.h',
    'cmb_timeseries.h',
    'cmb_wtdsummary.h'
)