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

    add_test(NAME library-options COMMAND flow_contract_test options)
    add_test(NAME library-lifecycle COMMAND flow_contract_test lifecycle)
    add_test(NAME library-signals COMMAND flow_contract_test signals)
    add_test(NAME library-multictx-stop COMMAND flow_contract_test multictx-stop)
    add_test(NAME library-overlays COMMAND flow_contract_test overlay-guards)
    add_test(NAME library-exec-error COMMAND flow_contract_test exec-error)
    add_test(NAME library-examples COMMAND flow_contract_test exec-examples)
    add_test(NAME library-exec-entry COMMAND flow_contract_test exec-entry)
    add_test(NAME library-exec-fixtures COMMAND flow_contract_test exec-fixtures)
    set_tests_properties(library-examples PROPERTIES TIMEOUT 30)
    set_tests_properties(library-exec-fixtures PROPERTIES TIMEOUT 30)

    if(FLOW_TEST_CLI)
        add_executable(flow_test_cli IMPORTED)
        set_target_properties(flow_test_cli PROPERTIES
            IMPORTED_LOCATION "${FLOW_TEST_CLI}"
        )
        set(FLOW_TEST_CLI_COMMAND flow_test_cli)
    else()
        set(FLOW_TEST_CLI_COMMAND flow)
    endif()

    add_test(NAME cli-help COMMAND ${FLOW_TEST_CLI_COMMAND} --help)
    add_test(NAME cli-version COMMAND ${FLOW_TEST_CLI_COMMAND} --version)
    add_test(NAME cli-unknown COMMAND ${FLOW_TEST_CLI_COMMAND} --unknown)
    set_tests_properties(cli-unknown PROPERTIES WILL_FAIL TRUE)
    add_test(NAME cli-missing-file COMMAND ${FLOW_TEST_CLI_COMMAND})
    set_tests_properties(cli-missing-file PROPERTIES WILL_FAIL TRUE)
endif()
