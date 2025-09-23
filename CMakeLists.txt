cmake_minimum_required(VERSION 3.31.6)
project(cimba LANGUAGES C ASM)
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -O0 -Wall -Wextra -Wpedantic")
enable_language(ASM_NASM)

include_directories(include)
set(SOURCES src/cmb_data.c src/cmb_random.c src/cmb_event.c src/cmb_logger.c src/cmb_assert.c
        src/cmi_random_exp_zig.inc src/cmi_random_nor_zig.inc
        src/arch/cmb_random_hwseed_Win64.c
        src/cmi_coroutine.c
        src/arch/cmb_random_hwseed_Win64.c
        src/arch/cmi_coroutine_context_Win64.c)
set(HEADERS include/cmb_data.h include/cmb_random.h include/cmb_event.h include/cmb_logger.h include/cmi_config.h include/cmi_memutils.h
        include/cmb_assert.h include/cmi_coroutine.h)

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

add_executable(cmi_test_coroutine test/cmi_test_coroutine.c test/cmi_test.h)
target_link_libraries(cmi_test_coroutine PRIVATE cimba)