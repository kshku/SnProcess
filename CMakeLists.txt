cmake_minimum_required(VERSION 3.15...3.31)

project(SnProcess VERSION 0.1.0 DESCRIPTION "Process and pipe abstraction library" LANGUAGES C)

include(FetchContent)
FetchContent_Declare(sncore
    GIT_REPOSITORY https://github.com/kshku/SnCore.git
    GIT_TAG v0.3.2
)
FetchContent_Declare(sntime
    GIT_REPOSITORY https://github.com/kshku/SnTime.git
    GIT_TAG v0.2.3
)
FetchContent_MakeAvailable(sncore sntime)

set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 17)

add_library(sn_process_configs INTERFACE)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(sn_process_configs INTERFACE SN_DEBUG)
endif()

set(gcc_clang_comp "$<COMPILE_LANG_AND_ID:C,Clang,GNU>")
set(msvc_comp "$<COMPILE_LANG_AND_ID:C,MSVC>")

target_compile_options(sn_process_configs INTERFACE "$<${gcc_clang_comp}:-Wall;-Wextra;-Wformat;-Wuninitialized;-Wpedantic>")
target_compile_options(sn_process_configs INTERFACE "$<${gcc_clang_comp}:-Wconversion;-Wsign-conversion>")

target_compile_options(sn_process_configs INTERFACE "$<${gcc_clang_comp}:-fvisibility=hidden>")

target_compile_definitions(sn_process_configs INTERFACE
    SN_PROCESS_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    SN_PROCESS_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    SN_PROCESS_VERSION_PATCH=${PROJECT_VERSION_PATCH}
)

option(SN_PROCESS_BUILD_SHARED "Build shared library" OFF)
option(SN_PROCESS_BUILD_TEST "Build tests" OFF)

add_subdirectory(docs)
add_subdirectory(process)

if(SN_PROCESS_BUILD_TEST)
    add_subdirectory("test")
else()
    message(STATUS "Building test is disabled")
endif()
