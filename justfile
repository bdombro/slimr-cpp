# slimr-cpp tasks
set shell := ["bash", "-eu", "-o", "pipefail", "-c"]

CXX := "clang++"
CXXFLAGS := "-std=c++26 -Wall -Wextra -Wno-unused-parameter -Iinclude -Ivendor/glaze/include -Ivendor -I../vendor/glaze/include -I../vendor"
LDFLAGS := "-lsqlite3 -lcurl"

# List recipes
_:
    @just --list

# Run comprehensive test suite with AddressSanitizer and UndefinedBehaviorSanitizer
test:
    mkdir -p bin
    {{CXX}} {{CXXFLAGS}} -fsanitize=address,undefined -g tests/test_main.cpp {{LDFLAGS}} -o bin/test_runner
    ./bin/test_runner

# Build optimized release test runner
build:
    mkdir -p bin
    {{CXX}} {{CXXFLAGS}} -O3 -flto tests/test_main.cpp {{LDFLAGS}} -o bin/test_runner_opt
    ./bin/test_runner_opt

# Clean build artifacts
clean:
    rm -rf bin *.o *.gch
