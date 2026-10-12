cmake_minimum_required(VERSION 3.5)
project(cadapt LANGUAGES CXX)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "CAdapt requires Linux ELF and kernel module interfaces")
endif()

option(CADAPT_STRICT "Treat compiler warnings and language extensions as errors" OFF)

function(cadapt_configure_target target)
    set_target_properties(${target} PROPERTIES CXX_STANDARD 11 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS YES)
    target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/common)
    if(CADAPT_STRICT AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror -pedantic-errors)
    endif()
endfunction()

add_executable(cadapt
    main.cpp OperGlibc.cpp OperKmod.cpp
    common/ElfPtrs.cpp common/ErrorLog.cpp
    kmod/Common.cpp kmod/ElfSymInfos.cpp kmod/ExistingModInfo.cpp kmod/KSymInfos.cpp
)
cadapt_configure_target(cadapt)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set_property(TARGET cadapt APPEND_STRING PROPERTY LINK_FLAGS " -static-libgcc -static-libstdc++")
endif()

include(CTest)
if(BUILD_TESTING)
    find_program(PYTHON3_EXECUTABLE NAMES python3)
    if(NOT PYTHON3_EXECUTABLE)
        message(FATAL_ERROR "Tests need Python 3.7+; use -DBUILD_TESTING=OFF to build only cadapt")
    endif()
    execute_process(COMMAND ${PYTHON3_EXECUTABLE} -c "import sys; sys.exit(sys.version_info < (3, 7))"
        RESULT_VARIABLE python_version_status)
    if(NOT python_version_status EQUAL 0)
        message(FATAL_ERROR "Tests need Python 3.7+")
    endif()
    foreach(suite cli glibc kmod elf_validation io system_modules logging)
        add_test(NAME ${suite}_regression
            COMMAND ${PYTHON3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/${suite}_regression.py $<TARGET_FILE:cadapt>)
        set_tests_properties(${suite}_regression PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE)
    endforeach()
    add_executable(resource_ownership tests/resource_ownership.cpp)
    cadapt_configure_target(resource_ownership)
    add_test(NAME resource_ownership COMMAND resource_ownership)
endif()
