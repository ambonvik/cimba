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
                'cmi_resourcebase.c'
)

inc_int = include_directories('.')

# Internal headers installed alongside the public API.
#
# These are not a user-facing API (hence the cmi_ prefix and the "do not call
# directly" contract), but they must be installed because the public cmb_*.h
# headers include them transitively: a public struct embeds an internal type by
# value (e.g. struct cmb_process embeds struct cmi_coroutine as its first
# member), so the compiler needs the complete internal definition to lay out the
# public one. An installed public header that reaches a non-installed header
# will not compile downstream.
#
# Rule of thumb: every header reachable by #include from a public header must
# appear here. The linux-install CI job enforces this by compiling each public
# header standalone against the installed tree only (see test/tools/verify_install.sh).
install_headers(
    'cmi_config.h',
    'cmi_coroutine.h',
    'cmi_hashheap.h',
    'cmi_holdable.h',
    'cmi_slist.h',
    'cmi_mempool.h',
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

cimba_lib = library('cimba',
                    sources,
                    include_directories : [inc_api, inc_int],
                    link_args : cimba_link_args,
                    dependencies : [project_deps, codegen_deps],
                    install : true,
                    version : meson.project_version(),
                    soversion : '1',
                    native : true
)

my_lib_dep = declare_dependency(
                    link_with : cimba_lib,
                    include_directories : [inc_api, inc_int]
)
