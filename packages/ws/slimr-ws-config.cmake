include(CMakeFindDependencyMacro)
find_dependency(slimr-json CONFIG)
find_dependency(CURL CONFIG)
if(NOT TARGET slimr::ws)
    add_library(slimr::ws INTERFACE IMPORTED)
    set_target_properties(slimr::ws PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_LIST_DIR}/../../include"
        INTERFACE_COMPILE_FEATURES cxx_std_26
        INTERFACE_LINK_LIBRARIES "slimr::json;CURL::libcurl")
endif()
