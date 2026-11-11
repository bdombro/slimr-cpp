include(CMakeFindDependencyMacro)
find_dependency(CURL CONFIG)
if(NOT TARGET slimr::curl)
    add_library(slimr::curl INTERFACE IMPORTED)
    set_target_properties(slimr::curl PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_LIST_DIR}/../../include"
        INTERFACE_COMPILE_FEATURES cxx_std_26
        INTERFACE_LINK_LIBRARIES CURL::libcurl)
endif()
