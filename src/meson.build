# src/meson.build

# Platform independent library source code
sources = files('cimba.c',
                'cmb_assert.c',
                'cmb_buffer.c',
                'cmb_condition.c',
                'cmb_dataset.c',
                'cmb_datasummary.c',
                'cmb_event.c',
                'cmb_logger.c',
                'cmb_objectqueue.c',
                'cmb_priorityqueue.c',
                'cmb_process.c',
                'cmb_random.c',
                'cmb_resource.c',
                'cmb_resourceguard.c',
                'cmb_resourcepool.c',
                'cmb_timeseries.c',
                'cmb_wtdsummary.c',
                'cmi_coroutine.c',
                'cmi_hashheap.c',
                'cmi_holdable.c',
                'cmi_mempool.c',
                'cmi_memregistry.c',
                'cmi_resourcebase.c'
)

inc_int = include_directories('.')

# Internal headers installed alongside the public API.
# Every header reachable by #include from a public header must
# appear here. The linux-install CI job enforces this by compiling each public
# header standalone against the installed tree only (see test/tools/verify_install.sh).
install_headers(
    'cmi_config.h',
    'cmi_coroutine.h',
    'cmi_dlist.h',
    'cmi_hashheap.h',
    'cmi_holdable.h',
    'cmi_slist.h',
    'cmi_memregistry.h',
    'cmi_memutils.h',
    'cmi_resourcebase.h'
)

# Platform and operating system dependent source code
subdir('port')

cimba_link_args = []
if host_machine.system() == 'windows'
    # Bundle static GCC libraries to avoid DLL confusion
    cimba_link_args += ['-static-libgcc', '-static']
endif

# Link-time optimisation of the library, by default in release builds only
# Not available on Windows due to the usual DLL issues
cimba_lto = []
cimba_lto_args = []
lto_feature = get_option('lto')
if host_machine.system() == 'windows'
    cimba_lto = ['b_lto=false']
    if lto_feature.enabled()
        warning('-Dlto=enabled is ignored on Windows: LTO breaks MinGW DLL exports')
    endif
elif lto_feature.enabled() or (lto_feature.auto() and get_option('buildtype').startswith('release'))
    cimba_lto = ['b_lto=true']
    if get_option('default_library') != 'shared'
        cimba_lto_args = ['-ffat-lto-objects']
    endif
endif

cimba_lib = library('cimba',
                    sources,
                    include_directories : [inc_api, inc_int],
                    link_args : cimba_link_args,
                    dependencies : [project_deps, codegen_deps],
                    install : true,
                    version : meson.project_version(),
                    soversion : '1',
                    native : true,
                    c_args : cimba_lto_args,
                    override_options : cimba_lto
)

my_lib_dep = declare_dependency(
                    link_with : cimba_lib,
                    include_directories : [inc_api, inc_int]
)
