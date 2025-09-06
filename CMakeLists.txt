cmake_minimum_required(VERSION 3.31.6)
project(cimba LANGUAGES C ASM)
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -O0 -Wall -Wextra -Wpedantic")

include_directories(include)
set(SOURCES src/cmb_data.c src/cmb_random.c src/cmb_event.c src/cmb_logger.c src/cmb_assert.c src/cmi_random_exp_zig.inc src/cmi_random_nor_zig.inc )
set(HEADERS include/cmb_data.h include/cmb_random.h include/cmb_event.h include/cmb_logger.h include/cmb_config.h include/cmi_memutils.h
        include/cmb_assert.h)

add_library(cimba STATIC ${SOURCES} ${HEADERS})

add_executable(cmi_test_data test/cmi_test_data.c test/cmi_test.h)
target_link_libraries(cmi_test_data PRIVATE cimba)

add_executable(cmi_test_random test/cmi_test_random.c test/cmi_test.h)
target_link_libraries(cmi_test_random PRIVATE cimba)

add_executable(cmi_test_event test/cmi_test_event.c test/cmi_test.h)
target_link_libraries(cmi_test_event PRIVATE cimba)

add_executable(cmi_test_logger test/cmi_test_logger.c test/cmi_test.h)
target_link_libraries(cmi_test_logger PRIVATE cimba)
