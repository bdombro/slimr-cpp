include(CMakeFindDependencyMacro)
find_dependency(slimr-json CONFIG)
if(NOT TARGET slimr::yaml)
    add_library(slimr::yaml INTERFACE IMPORTED)
    set_target_properties(slimr::yaml PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_LIST_DIR}/../../include"
        INTERFACE_COMPILE_FEATURES cxx_std_26
        INTERFACE_LINK_LIBRARIES slimr::json)
endif()
