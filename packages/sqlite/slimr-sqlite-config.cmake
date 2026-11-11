include(CMakeFindDependencyMacro)
find_dependency(slimr-json CONFIG)
find_dependency(unofficial-sqlite3 CONFIG)
if(NOT TARGET slimr::sqlite)
    add_library(slimr::sqlite INTERFACE IMPORTED)
    set_target_properties(slimr::sqlite PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_LIST_DIR}/../../include"
        INTERFACE_COMPILE_FEATURES cxx_std_26
        INTERFACE_LINK_LIBRARIES "slimr::json;unofficial::sqlite3::sqlite3")
endif()
