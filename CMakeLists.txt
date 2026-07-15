cmake_minimum_required(VERSION 3.24)

project(AstraRuntime VERSION 0.1.0 LANGUAGES CXX)

include(FetchContent)
set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(zlib
    GIT_REPOSITORY https://github.com/madler/zlib.git
    GIT_TAG v1.3.1
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(zlib)

option(ASTRA_BUILD_TESTS "Build Astra unit tests" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(astra_core STATIC
    src/package_store.cpp
    src/package_bundle.cpp
    src/sha256.cpp
    src/zip_archive.cpp
)

target_include_directories(astra_core PUBLIC include)
target_compile_definitions(astra_core PUBLIC
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)
target_compile_options(astra_core PRIVATE /W4 /permissive- /utf-8)
target_include_directories(astra_core PRIVATE ${zlib_SOURCE_DIR} ${zlib_BINARY_DIR})
target_link_libraries(astra_core PUBLIC bcrypt zlibstatic)

add_executable(astra WIN32 src/launcher_main.cpp)
target_compile_options(astra PRIVATE /W4 /permissive- /utf-8)
target_link_libraries(astra PRIVATE astra_core comctl32 ole32 shell32)

add_executable(astra-cli src/cli_main.cpp)
target_compile_options(astra-cli PRIVATE /W4 /permissive- /utf-8)
target_link_libraries(astra-cli PRIVATE astra_core)

add_executable(astra-runtime WIN32 src/runtime_main.cpp)
target_compile_options(astra-runtime PRIVATE /W4 /permissive- /utf-8)
target_link_libraries(astra-runtime PRIVATE astra_core comctl32 shell32)

install(TARGETS astra astra-cli astra-runtime
    RUNTIME DESTINATION .
)
install(FILES README.md THIRD_PARTY_NOTICES.md DESTINATION .)

if(ASTRA_BUILD_TESTS)
    enable_testing()
    add_executable(astra-tests tests/package_store_tests.cpp)
    target_compile_options(astra-tests PRIVATE /W4 /permissive- /utf-8)
    target_link_libraries(astra-tests PRIVATE astra_core)
    add_test(NAME astra-package-store COMMAND astra-tests)

    add_executable(astra-zip-tests tests/zip_archive_tests.cpp)
    target_compile_options(astra-zip-tests PRIVATE /W4 /permissive- /utf-8)
    target_link_libraries(astra-zip-tests PRIVATE astra_core)
    add_test(NAME astra-zip-archive COMMAND astra-zip-tests)
endif()
