# include/meson.build

inc_api = include_directories('.')

# Main catchall header
install_headers('cimba.h')

# Public API headers - installed to include/cimba/
install_headers(
    'cmb_assert.h',
    'cmb_buffer.h',
    'cmb_condition.h',
    'cmb_dataset.h',
    'cmb_datasummary.h',
    'cmb_event.h',
    'cmb_logger.h',
    'cmb_objectqueue.h',
    'cmb_process.h',
    'cmb_random.h',
    'cmb_resource.h',
    'cmb_resourceguard.h',
    'cmb_resourcestore.h',
    'cmb_timeseries.h',
    'cmb_wtdsummary.h',
    subdir : 'cimba'  # headers will be installed to <prefix>/include/cimba/
)