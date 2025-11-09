cmake_minimum_required(VERSION 3.31.6)
project(cimba LANGUAGES C ASM)
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -O0 -Wall -Wextra")
enable_language(ASM_NASM)

include_directories(include src)
set(SOURCES src/cmb_dataset.c
        src/cmb_event.c
        src/cmb_resource.c
        src/cmb_store.c
        src/cmb_process.c
        src/cmb_random.c
        src/cmb_assert.c
        src/cmb_buffer.c
        src/cmb_mempool.c
        src/cmb_logger.c
        src/cmb_datasummary.c
        src/cmb_wtdsummary.c
        src/cmb_timeseries.c
        src/cmb_queue.c
        src/cmi_coroutine.c
        src/cmi_hashheap.c
        src/cmi_processtag.c
        src/cmi_resourcetag.c
        src/cmi_resourcebase.c
        src/cmi_resourceguard.c
        src/cmi_random_exp_zig.inc
        src/cmi_random_nor_zig.inc
        src/arch/cmi_memutils_Win64.c
        src/arch/cmb_random_hwseed_Win64.c
        src/arch/cmi_coroutine_context_Win64.c
        src/cmi_resourceguard.c
        src/cmi_resourceguard.h
        src/cmi_resourcebase.c
        src/cmi_resourcebase.h)
set(HEADERS include/cmb_dataset.h
        include/cmb_event.h
        include/cmb_random.h
        include/cmb_assert.h
        include/cmb_logger.h
        include/cmb_buffer.h
        include/cmb_mempool.h
        include/cmb_resource.h
        include/cmb_store.h
        include/cmb_queue.h
        include/cmb_buffer.h
        include/cmb_datasummary.h
        include/cmb_wtdsummary.h
        include/cmb_timeseries.h
        src/cmi_coroutine.h
        src/cmi_config.h
        src/cmi_hashheap.h
        src/cmi_memutils.h
        src/cmi_processtag.h
        src/cmi_resourcetag.h
        src/cmi_resourceguard.h
        src/cmi_resourcebase.h
        src/cmi_dataset.h)

set(ASM_SOURCES src/arch/cmi_coroutine_context_Win64.asm src/arch/cmi_random_hwseed_Win64.asm)
set_source_files_properties(${ASM_SOURCES} PROPERTIES LANGUAGE ASM_NASM)

add_library(cimba STATIC ${SOURCES} ${ASM_SOURCES} ${HEADERS})

add_executable(cmi_test_data test/cmi_test_data.c test/cmi_test.h)
target_link_libraries(cmi_test_data PRIVATE cimba)

add_executable(cmi_test_random test/cmi_test_random.c test/cmi_test.h)
target_link_libraries(cmi_test_random PRIVATE cimba)

add_executable(cmi_test_event test/cmi_test_event.c test/cmi_test.h)
target_link_libraries(cmi_test_event PRIVATE cimba)

add_executable(cmi_test_logger test/cmi_test_logger.c test/cmi_test.h)
target_link_libraries(cmi_test_logger PRIVATE cimba)

add_executable(cmi_test_coroutine test/cmi_test_coroutine.c test/cmi_test.h )
target_link_libraries(cmi_test_coroutine PRIVATE cimba)

add_executable(cmi_test_mempool test/cmi_test_mempool.c test/cmi_test.h)
target_link_libraries(cmi_test_mempool PRIVATE cimba)

add_executable(cmi_test_process test/cmi_test_process.c test/cmi_test.h)
target_link_libraries(cmi_test_process PRIVATE cimba)

add_executable(cmi_test_hashheap test/cmi_test_hashheap.c test/cmi_test.h)
target_link_libraries(cmi_test_hashheap PRIVATE cimba)

add_executable(cmi_test_resource test/cmi_test_resource.c test/cmi_test.h)
target_link_libraries(cmi_test_resource PRIVATE cimba)

add_executable(cmi_test_store test/cmi_test_store.c test/cmi_test.h)
target_link_libraries(cmi_test_store PRIVATE cimba)

add_executable(cmi_test_buffer test/cmi_test_buffer.c test/cmi_test.h)
target_link_libraries(cmi_test_buffer PRIVATE cimba)

add_executable(cmi_test_queue test/cmi_test_queue.c test/cmi_test.h)
target_link_libraries(cmi_test_queue PRIVATE cimba)