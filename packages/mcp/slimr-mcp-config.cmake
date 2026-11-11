include(CMakeFindDependencyMacro)
find_dependency(slimr-json CONFIG)
find_dependency(slimr-reflect CONFIG)
find_dependency(slimr-bugsink CONFIG)
if(NOT TARGET slimr::mcp)
    add_library(slimr::mcp INTERFACE IMPORTED)
    set_target_properties(slimr::mcp PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_LIST_DIR}/../../include"
        INTERFACE_COMPILE_FEATURES cxx_std_26
        INTERFACE_LINK_LIBRARIES "slimr::json;slimr::reflect;slimr::bugsink")
endif()
