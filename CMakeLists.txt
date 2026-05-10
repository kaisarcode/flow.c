cmake_minimum_required(VERSION 3.14)
project(flow C)

set(CMAKE_C_STANDARD 11)

option(FLOW_NATIVE "Enable native hardware optimizations" OFF)

if(NOT MSVC)
    add_compile_options(-O3 -Wall -Wextra -Werror)
    if(FLOW_NATIVE)
        add_compile_options(-march=native)
    endif()
    add_definitions(-D_GNU_SOURCE)
else()
    add_compile_options(/O2 /W3 /WX)
endif()

add_library(flow_static STATIC src/libflow.c)
set_target_properties(flow_static PROPERTIES OUTPUT_NAME "flow")
target_include_directories(flow_static PUBLIC src)

add_library(flow_shared SHARED src/libflow.c)
set_target_properties(flow_shared PROPERTIES OUTPUT_NAME "flow")
target_include_directories(flow_shared PUBLIC src)

add_executable(flow src/flow.c)
target_include_directories(flow PRIVATE src)
target_link_libraries(flow flow_static)
