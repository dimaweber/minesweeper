
set(TARGET_PLUGIN_fields_list fields_list)
set(TARGET_PLUGIN_cell_check cell_check)

set(SOURCES_PLUGIN_FIELDS_LIST
        fields_list.cxx)
set(SOURCES_PLUGIN_CELL_CHECK
        cell_check.cxx)


add_library(${TARGET_PLUGIN_fields_list} SHARED ${SOURCES_PLUGIN_FIELDS_LIST})
target_link_libraries(${TARGET_PLUGIN_fields_list} PRIVATE -lfmt)
if (PKGCONFIG_restbed_FOUND)
    target_link_libraries(${TARGET_PLUGIN_fields_list} PRIVATE PkgConfig::restbed)
endif ()
set_target_properties(${TARGET_PLUGIN_fields_list} PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugins)

add_library(${TARGET_PLUGIN_cell_check} SHARED ${SOURCES_PLUGIN_CELL_CHECK})
target_link_libraries(${TARGET_PLUGIN_cell_check} PRIVATE -lfmt)
if (PKGCONFIG_restbed_FOUND)
    target_link_libraries(${TARGET_PLUGIN_cell_check} PRIVATE PkgConfig::restbed)
endif ()
set_target_properties(${TARGET_PLUGIN_cell_check} PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugins)