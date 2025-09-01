cmake_minimum_required(VERSION 3.31.6)
project(cimba LANGUAGES C ASM)
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -O0 -Wall -Wextra -Wpedantic")

include_directories(include)
set(SOURCES src/cmb_data.c src/cmb_random.c src/cmb_event.c src/cmb_logger.c src/cmb_assert.c src/cmi_random_exp_zig.inc src/cmi_random_nor_zig.inc )
set(HEADERS include/cmb_data.h include/cmb_random.h include/cmb_event.h include/cmb_logger.h include/cmb_config.h include/cmi_memutils.h
        include/cmb_assert.h)

add_executable(cmi_test_data src/test/cmi_test_data.c ${SOURCES} ${HEADERS}
        include/cmb_assert.h
        src/cmb_assert.c)
add_executable(cmi_test_random src/test/cmi_test_random.c ${SOURCES} ${HEADERS}
        include/cmb_assert.h)
add_executable(cmi_test_event src/test/cmi_test_event.c ${SOURCES} ${HEADERS}
        include/cmb_assert.h)
add_executable(cmi_test_logger src/test/cmi_test_logger.c ${SOURCES} ${HEADERS}
        include/cmb_assert.h)

