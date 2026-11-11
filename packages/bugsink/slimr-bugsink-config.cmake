include(CMakeFindDependencyMacro)
find_dependency(slimr-json CONFIG)
find_dependency(slimr-curl CONFIG)
if(NOT TARGET slimr::bugsink)
    add_library(slimr::bugsink INTERFACE IMPORTED)
    set_target_properties(slimr::bugsink PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_LIST_DIR}/../../include"
        INTERFACE_COMPILE_FEATURES cxx_std_26
        INTERFACE_LINK_LIBRARIES "slimr::json;slimr::curl")
endif()
