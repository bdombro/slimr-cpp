if(NOT TARGET slimr::encoding)
    add_library(slimr::encoding INTERFACE IMPORTED)
    set_target_properties(slimr::encoding PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_LIST_DIR}/../../include"
        INTERFACE_COMPILE_FEATURES cxx_std_26)
endif()
