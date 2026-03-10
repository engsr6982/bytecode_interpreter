cmake_minimum_required(VERSION 3.20)

project(bytecode_interpreter C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# core interpreter
add_library(interpreter STATIC
    interpreter.c
)

target_include_directories(interpreter
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# debug macro
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-g)
    target_compile_definitions(interpreter PRIVATE VM_DEBUG)
endif()

# test interpreter
add_executable(interpreter_test
    repl.c
)
target_link_libraries(interpreter_test
    PRIVATE
        interpreter
)

