vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO ithewei/libhv
    REF 5c35a3dac136b30b4550de0139d2428f555afa77
    SHA512 9e3aa1b26e3e49642e728a75fbae0e822d86cf01316955d2b1d9f5956bfa7e01ec7cdcf22ce8c32b97d4adc648a50a470515c76d2ba06e3f7819945a68ec310c
    HEAD_REF master
)

string(COMPARE EQUAL "${VCPKG_LIBRARY_LINKAGE}" "static" BUILD_STATIC)
string(COMPARE EQUAL "${VCPKG_LIBRARY_LINKAGE}" "dynamic" BUILD_SHARED)

vcpkg_check_features(OUT_FEATURE_OPTIONS FEATURE_OPTIONS
    FEATURES
        ssl WITH_OPENSSL
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    DISABLE_PARALLEL_CONFIGURE
    OPTIONS
        -DBUILD_EXAMPLES=OFF
        -DBUILD_UNITTEST=OFF
        -DBUILD_STATIC=${BUILD_STATIC}
        -DBUILD_SHARED=${BUILD_SHARED}
        -DWITH_HTTP_CLIENT=OFF
        -DWITH_CURL=OFF
        -DWITH_GNUTLS=OFF
        -DWITH_MBEDTLS=OFF
        -DWITH_NGHTTP2=OFF
        ${FEATURE_OPTIONS}
)

vcpkg_cmake_install()

vcpkg_cmake_config_fixup(CONFIG_PATH lib/cmake/libhv)
vcpkg_copy_pdbs()
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
