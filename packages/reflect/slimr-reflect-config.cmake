include(CMakeFindDependencyMacro)
find_dependency(slimr-json CONFIG)
if(NOT TARGET slimr::reflect)
    add_library(slimr::reflect INTERFACE IMPORTED)
    set_target_properties(slimr::reflect PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_LIST_DIR}/../../include"
        INTERFACE_COMPILE_FEATURES cxx_std_26
        INTERFACE_LINK_LIBRARIES slimr::json)
endif()
