set shell := ["zsh", "-f", "-eu", "-o", "pipefail", "-c"]
set positional-arguments

architecture := replace(replace(arch(), "aarch64", "arm64"), "x86_64", "x64")
system := if os() == "macos" { "osx" } else { os() }
triplet := f'{{architecture}}-{{system}}'
cxx := env("CXX", if os() == "macos" { "/usr/bin/clang++" } else { "clang++" })

_:
    @just --list

deps:
    : "${VCPKG_ROOT:?Set VCPKG_ROOT to a bootstrapped checkout}"
    vcpkg install --triplet={{triplet}} --x-install-root=build/deps --x-wait-for-lock

configure configuration="Debug": deps
    @case "$1" in Debug|Release) ;; *) echo "Expected Debug or Release" >&2; exit 1;; esac
    CXX="{{cxx}}" cmake -S . -B "build/cmake/$1" -G Ninja -DCMAKE_BUILD_TYPE="$1" -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" -DVCPKG_MANIFEST_MODE=OFF -DVCPKG_MANIFEST_INSTALL=OFF -DVCPKG_INSTALLED_DIR="{{justfile_directory()}}/build/deps" -DVCPKG_TARGET_TRIPLET={{triplet}} -DENABLE_SANITIZERS={{if configuration == "Debug" { "ON" } else { "OFF" }}}

test: (configure "Debug")
    cmake --build build/cmake/Debug --parallel 2
    ctest --test-dir build/cmake/Debug --output-on-failure --no-tests=error

test-pkg name:
    @case "$1" in json|reflect|yaml|sqlite|curl|bugsink|ws|mcp|encoding|proc|time|text|test) ;; *) echo "Unknown package: $1" >&2; exit 1;; esac
    just configure Debug
    cmake --build build/cmake/Debug --target "$1_test" --parallel 2
    ctest --test-dir build/cmake/Debug --output-on-failure --no-tests=error -R "^$1$"

build: (configure "Release")
    cmake --build build/cmake/Release --parallel 2
    ctest --test-dir build/cmake/Release --output-on-failure --no-tests=error

build-pkg name:
    @case "$1" in json|reflect|yaml|sqlite|curl|bugsink|ws|mcp|encoding|proc|time|text|test) ;; *) echo "Unknown package: $1" >&2; exit 1;; esac
    just configure Release
    cmake --build build/cmake/Release --target "$1_test" --parallel 2
    ctest --test-dir build/cmake/Release --output-on-failure --no-tests=error -R "^$1$"

lint: (configure "Debug")
    "${CLANG_TIDY:-clang-tidy}" -p build/cmake/Debug tests/test_main.cpp packages/*/test.cpp

lint-pkg name:
    @case "$1" in json|reflect|yaml|sqlite|curl|bugsink|ws|mcp|encoding|proc|time|text|test) ;; *) echo "Unknown package: $1" >&2; exit 1;; esac
    just configure Debug
    "${CLANG_TIDY:-clang-tidy}" -p build/cmake/Debug "packages/$1/test.cpp"

publish *args:
    node scripts/publish.mjs "$@"

clean:
    rm -rf build
