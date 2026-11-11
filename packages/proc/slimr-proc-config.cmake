if(NOT TARGET slimr::proc)
    add_library(slimr::proc INTERFACE IMPORTED)
    set_target_properties(slimr::proc PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_LIST_DIR}/../../include"
        INTERFACE_COMPILE_FEATURES cxx_std_26)
endif()
