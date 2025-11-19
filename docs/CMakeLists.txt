
find_package(Doxygen)

if (DOXYGEN_FOUND)
    message(STATUS "Doxygen found, verison ${DOXYGEN_VERSION}")
    set(DOXYGEN_INPUT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)
    set(DOXYGEN_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/doxygen)
    set(DOXYGEN_INDEX_FILE ${DOXYGEN_OUTPUT_DIR}/xml/index.xml)
    set(DOXYFILE_IN ../Doxyfile.in)
    set(DOXYFILE_OUT ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile)

    configure_file(${DOXYFILE_IN} ${DOXYFILE_OUT} @ONLY)

    file(MAKE_DIRECTORY ${DOXYGEN_OUTPUT_DIR})

    add_custom_command(OUTPUT ${DOXYGEN_INDEX_FILE}
            COMMAND ${DOXYGEN_EXECUTABLE} ${DOXYFILE_OUT}
            MAIN_DEPENDENCY ${DOXYGEN_IN}
            DEPENDS ${HEADERS}
            COMMENT "Generating API documentation"
            VERBATIM )

    add_custom_target(docs ALL DEPENDS ${DOXYGEN_INDEX_FILE})
else()
    message(STATUS "Doxygen not found")
endif()