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
    src/android_framework.cpp
    src/android_vm.cpp
    src/package_store.cpp
    src/android_manifest.cpp
    src/apk_signature.cpp
    src/dex_catalog.cpp
    src/dex_executor.cpp
    src/google_services.cpp
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
target_link_libraries(astra_core PUBLIC bcrypt crypt32 zlibstatic)

add_executable(astra WIN32 src/launcher_main.cpp)
target_compile_options(astra PRIVATE /W4 /permissive- /utf-8)
target_link_libraries(astra PRIVATE astra_core comctl32 ole32 shell32)

add_executable(astra-cli src/cli_main.cpp)
target_compile_options(astra-cli PRIVATE /W4 /permissive- /utf-8)
target_link_libraries(astra-cli PRIVATE astra_core)

add_executable(astra-runtime WIN32 src/android_runtime_main.cpp)
target_compile_options(astra-runtime PRIVATE /W4 /permissive- /utf-8)
target_link_libraries(astra-runtime PRIVATE astra_core comctl32 shell32)

install(TARGETS astra astra-cli astra-runtime
    RUNTIME DESTINATION .
)
install(FILES README.md THIRD_PARTY_NOTICES.md DESTINATION .)
install(FILES docs/ARCHITECTURE.md docs/GOOGLE_SERVICES.md docs/VIRTUALIZATION.md DESTINATION docs)
install(FILES tools/bootstrap-android-vm.ps1 tools/enable-windows-hypervisor.ps1 DESTINATION tools)

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

    add_executable(astra-dex-tests tests/dex_catalog_tests.cpp)
    target_compile_options(astra-dex-tests PRIVATE /W4 /permissive- /utf-8)
    target_link_libraries(astra-dex-tests PRIVATE astra_core)
    add_test(NAME astra-dex-catalog COMMAND astra-dex-tests)

    add_executable(astra-manifest-tests tests/android_manifest_tests.cpp)
    target_compile_options(astra-manifest-tests PRIVATE /W4 /permissive- /utf-8)
    target_link_libraries(astra-manifest-tests PRIVATE astra_core)
    add_test(NAME astra-android-manifest COMMAND astra-manifest-tests)

    add_executable(astra-google-services-tests tests/google_services_tests.cpp)
    target_compile_options(astra-google-services-tests PRIVATE /W4 /permissive- /utf-8)
    target_link_libraries(astra-google-services-tests PRIVATE astra_core)
    add_test(NAME astra-google-services COMMAND astra-google-services-tests)

    add_executable(astra-dex-executor-tests tests/dex_executor_tests.cpp)
    target_compile_options(astra-dex-executor-tests PRIVATE /W4 /permissive- /utf-8)
    target_link_libraries(astra-dex-executor-tests PRIVATE astra_core)
    add_test(NAME astra-dex-executor COMMAND astra-dex-executor-tests)

    add_executable(astra-android-framework-tests tests/android_framework_tests.cpp)
    target_compile_options(astra-android-framework-tests PRIVATE /W4 /permissive- /utf-8)
    target_link_libraries(astra-android-framework-tests PRIVATE astra_core)
    add_test(NAME astra-android-framework COMMAND astra-android-framework-tests)

    add_executable(astra-android-vm-tests tests/android_vm_tests.cpp)
    target_compile_options(astra-android-vm-tests PRIVATE /W4 /permissive- /utf-8)
    target_link_libraries(astra-android-vm-tests PRIVATE astra_core)
    add_test(NAME astra-android-vm COMMAND astra-android-vm-tests)
endif()
