cmake_minimum_required(VERSION 3.20)
project(gta2_recomp C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

set(WINRECOMP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../winrecomp" CACHE PATH "winrecomp toolkit")
set(RECOMP_GEN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/gen/src" CACHE PATH "generated C (tools/lift.sh)")

add_subdirectory(${WINRECOMP_DIR}/runtime ${CMAKE_BINARY_DIR}/winrecomp_rt)

file(GLOB RECOMP_GEN_SOURCES CONFIGURE_DEPENDS "${RECOMP_GEN_DIR}/*.c")
if(NOT RECOMP_GEN_SOURCES)
    message(FATAL_ERROR "No generated C in ${RECOMP_GEN_DIR} - run tools/lift.sh first")
endif()

# The generated code: one object library, optimised even in debug builds
# (an unoptimised 1.1M-line translation is unusably slow).
add_library(gta2_gen OBJECT ${RECOMP_GEN_SOURCES})
target_include_directories(gta2_gen PRIVATE ${RECOMP_GEN_DIR})
if(NOT MSVC)
    target_compile_options(gta2_gen PRIVATE -O2 -w -fwrapv -fno-strict-aliasing)
endif()

add_executable(gta2_recomp
    src/main.c
    src/gta2_registry.c
    src/gta2_manual.c
    $<TARGET_OBJECTS:gta2_gen>)
target_link_libraries(gta2_recomp PRIVATE winrecomp_rt)
if(NOT MSVC)
    target_compile_options(gta2_recomp PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
# Crash reports name the generated functions (dladdr needs them exported).
set_target_properties(gta2_recomp PROPERTIES ENABLE_EXPORTS ON)
