cmake_minimum_required(VERSION 3.12)
project(cimba LANGUAGES C ASM)
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -O0 -Wall -Wextra")
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O3")
enable_language(ASM_NASM)

include_directories(include src)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/docs)

set(API_HEADERS
        include/cimba.h
        include/cmb_assert.h
        include/cmb_buffer.h
        include/cmb_dataset.h
        include/cmb_datasummary.h
        include/cmb_event.h
        include/cmb_logger.h
        include/cmb_mempool.h
        include/cmb_objectqueue.h
        include/cmb_process.h
        include/cmb_random.h
        include/cmb_resource.h
        include/cmb_store.h
        include/cmb_wtdsummary.h
        include/cmb_timeseries.h
)

set(HEADERS
        src/cmi_coroutine.h
        src/cmi_config.h
        src/cmi_dataset.h
        src/cmi_hashheap.h
        src/cmi_memutils.h
        src/cmi_processtag.h
        src/cmi_resourcebase.h
        src/cmi_resourceguard.h
        src/cmi_resourcetag.h
)

set(SOURCES
        src/cimba.c
        src/cmb_assert.c
        src/cmb_buffer.c
        src/cmb_dataset.c
        src/cmb_datasummary.c
        src/cmb_event.c
        src/cmb_logger.c
        src/cmb_mempool.c
        src/cmb_objectqueue.c
        src/cmb_process.c
        src/cmb_random.c
        src/cmb_resource.c
        src/cmb_store.c
        src/cmb_timeseries.c
        src/cmb_wtdsummary.c
        src/cmi_coroutine.c
        src/cmi_hashheap.c
        src/cmi_processtag.c
        src/cmi_resourcebase.c
        src/cmi_resourceguard.c
        src/cmi_resourcetag.c
        src/cmi_random_exp_zig.inc
        src/cmi_random_nor_zig.inc
        src/arch/cmi_memutils_Win64.c
        src/arch/cmb_random_hwseed_Win64.c
        src/arch/cmi_coroutine_context_Win64.c
        src/arch/cmi_cpu_cores_Win64.c
)

set(ASM_SOURCES src/arch/cmi_coroutine_context_Win64.asm src/arch/cmi_random_hwseed_Win64.asm)
set_source_files_properties(${ASM_SOURCES} PROPERTIES LANGUAGE ASM_NASM)

add_library(cimba STATIC ${API_HEADERS} ${HEADERS} ${SOURCES} ${ASM_SOURCES})

add_executable(test_data test/test_data.c test/test.h)
target_link_libraries(test_data PRIVATE cimba)

add_executable(test_random test/test_random.c test/test.h)
target_link_libraries(test_random PRIVATE cimba)

add_executable(test_event test/test_event.c test/test.h)
target_link_libraries(test_event PRIVATE cimba)

add_executable(test_logger test/test_logger.c test/test.h)
target_link_libraries(test_logger PRIVATE cimba)

add_executable(test_coroutine test/test_coroutine.c test/test.h)
target_link_libraries(test_coroutine PRIVATE cimba)

add_executable(test_mempool test/test_mempool.c test/test.h)
target_link_libraries(test_mempool PRIVATE cimba)

add_executable(test_process test/test_process.c test/test.h)
target_link_libraries(test_process PRIVATE cimba)

add_executable(test_hashheap test/test_hashheap.c test/test.h)
target_link_libraries(test_hashheap PRIVATE cimba)

add_executable(test_resource test/test_resource.c test/test.h)
target_link_libraries(test_resource PRIVATE cimba)

add_executable(test_store test/test_store.c test/test.h)
target_link_libraries(test_store PRIVATE cimba)

add_executable(test_buffer test/test_buffer.c test/test.h)
target_link_libraries(test_buffer PRIVATE cimba)

add_executable(test_objectqueue test/test_objectqueue.c test/test.h)
target_link_libraries(test_objectqueue PRIVATE cimba)

add_executable(test_cimba test/test_cimba.c test/test.h)
target_link_libraries(test_cimba PRIVATE cimba pthread)

