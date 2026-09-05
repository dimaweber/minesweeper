
set(TARGET_PLUGIN_boards_list boards_list)
set(TARGET_PLUGIN_cell_check cell_check)

set(SOURCES_PLUGIN_BOARDS_LIST
        boards_list.cxx)
set(SOURCES_PLUGIN_CELL_CHECK
        cell_check.cxx)


add_library(${TARGET_PLUGIN_boards_list} SHARED ${SOURCES_PLUGIN_BOARDS_LIST})
target_link_libraries(${TARGET_PLUGIN_boards_list} PRIVATE -lfmt)
if (PKGCONFIG_restbed_FOUND)
    target_link_libraries(${TARGET_PLUGIN_boards_list} PRIVATE PkgConfig::restbed)
endif ()
set_target_properties(${TARGET_PLUGIN_boards_list} PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugins)

add_library(${TARGET_PLUGIN_cell_check} SHARED ${SOURCES_PLUGIN_CELL_CHECK})
target_link_libraries(${TARGET_PLUGIN_cell_check} PRIVATE -lfmt)
if (PKGCONFIG_restbed_FOUND)
    target_link_libraries(${TARGET_PLUGIN_cell_check} PRIVATE PkgConfig::restbed)
endif ()
set_target_properties(${TARGET_PLUGIN_cell_check} PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugins)