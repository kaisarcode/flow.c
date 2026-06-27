cmake_minimum_required(VERSION 3.14)
project(flow C)

set(CMAKE_C_STANDARD 11)

if(NOT DEFINED FLOW_BUILD_VERSION)
    set(FLOW_BUILD_VERSION 0)
endif()

option(FLOW_NATIVE "Enable native hardware optimizations" OFF)
option(FLOW_BUILD_TESTS "Build flow tests" OFF)

set(FLOW_TEST_EXTERNAL_ARTIFACTS OFF)
if(FLOW_BUILD_TESTS AND FLOW_TEST_SHARED_LIBRARY)
    set(FLOW_TEST_EXTERNAL_ARTIFACTS ON)
endif()

if(NOT MSVC)
    add_compile_options(-O3 -Wall -Wextra -Werror)
    if(FLOW_NATIVE)
        add_compile_options(-march=native)
    endif()
    add_definitions(-D_GNU_SOURCE)
else()
    add_compile_options(/O2 /W3 /WX)
endif()

set_source_files_properties(src/libflow.c PROPERTIES
    COMPILE_DEFINITIONS KC_FLOW_BUILD_VERSION=${FLOW_BUILD_VERSION}
)

if(NOT FLOW_TEST_EXTERNAL_ARTIFACTS)
    add_library(flow_static STATIC src/libflow.c)
    set_target_properties(flow_static PROPERTIES OUTPUT_NAME "flow")
    target_include_directories(flow_static PUBLIC src)

    add_library(flow_shared SHARED src/libflow.c)
    set_target_properties(flow_shared PROPERTIES OUTPUT_NAME "flow")
    set_target_properties(flow_shared PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
    target_include_directories(flow_shared PUBLIC src)

    add_executable(flow src/flow.c)
    target_include_directories(flow PRIVATE src)
    target_link_libraries(flow flow_static)
endif()

if(FLOW_BUILD_TESTS)
    enable_testing()

    if(FLOW_TEST_SHARED_LIBRARY)
        add_library(flow_test_shared SHARED IMPORTED)
        set_target_properties(flow_test_shared PROPERTIES
            IMPORTED_LOCATION "${FLOW_TEST_SHARED_LIBRARY}"
        )
        if(FLOW_TEST_IMPORT_LIBRARY)
            set_target_properties(flow_test_shared PROPERTIES
                IMPORTED_IMPLIB "${FLOW_TEST_IMPORT_LIBRARY}"
            )
        endif()
    else()
        set(FLOW_TEST_SHARED_TARGET flow_shared)
    endif()

    if(TARGET flow_test_shared)
        set(FLOW_TEST_SHARED_TARGET flow_test_shared)
    endif()

    add_executable(flow_contract_test src/test.c)
    target_include_directories(flow_contract_test PRIVATE src)
    target_link_libraries(flow_contract_test PRIVATE ${FLOW_TEST_SHARED_TARGET})
    target_compile_definitions(flow_contract_test PRIVATE
        FLOW_TEST_ETC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/etc"
    )

    add_custom_command(TARGET flow_contract_test POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:${FLOW_TEST_SHARED_TARGET}>
            $<TARGET_FILE_DIR:flow_contract_test>
    )

    add_test(NAME library-kc_flow_open COMMAND flow_contract_test kc_flow_open)
    add_test(NAME library-kc_flow_options_default COMMAND flow_contract_test kc_flow_options_default)
    add_test(NAME library-kc_flow_options_load_env COMMAND flow_contract_test kc_flow_options_load_env)
    add_test(NAME library-kc_flow_options_free COMMAND flow_contract_test kc_flow_options_free)
    add_test(NAME library-kc_flow_on_signal COMMAND flow_contract_test kc_flow_on_signal)
    add_test(NAME library-kc_flow_raise_signal COMMAND flow_contract_test kc_flow_raise_signal)
    add_test(NAME library-kc_flow_listen_signals COMMAND flow_contract_test kc_flow_listen_signals)
    add_test(NAME library-kc_flow_listen_signal COMMAND flow_contract_test kc_flow_listen_signal)
    add_test(NAME library-kc_flow_signal_listener COMMAND flow_contract_test kc_flow_signal_listener)
    add_test(NAME library-kc_flow_close COMMAND flow_contract_test kc_flow_close)
    add_test(NAME library-kc_flow_stop COMMAND flow_contract_test kc_flow_stop)
    add_test(NAME library-kc_flow_set COMMAND flow_contract_test kc_flow_set)
    add_test(NAME library-kc_flow_unset COMMAND flow_contract_test kc_flow_unset)
    add_test(NAME library-kc_flow_exec COMMAND flow_contract_test kc_flow_exec)
    add_test(NAME library-kc_flow_exec_entry COMMAND flow_contract_test kc_flow_exec_entry)
    add_test(NAME library-kc_flow_free COMMAND flow_contract_test kc_flow_free)
    add_test(NAME library-kc_flow_version COMMAND flow_contract_test kc_flow_version)
    add_test(NAME library-kc_flow_strerror COMMAND flow_contract_test kc_flow_strerror)
    set_tests_properties(library-kc_flow_exec PROPERTIES TIMEOUT 30)
    set_tests_properties(library-kc_flow_exec_entry PROPERTIES TIMEOUT 30)
    set_tests_properties(library-kc_flow_set PROPERTIES TIMEOUT 30)
    set_tests_properties(library-kc_flow_unset PROPERTIES TIMEOUT 30)
endif()
