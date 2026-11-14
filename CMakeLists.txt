cmake_minimum_required(VERSION 3.5)
project(DDSSmartGrid LANGUAGES C)

# idlc
find_program(IDL_COMPILER idlc REQUIRED)
message(STATUS "Found idlc compiler: ${IDL_COMPILER}")

find_package(CycloneDDS REQUIRED)

set(IDL_FILE "${CMAKE_CURRENT_SOURCE_DIR}/SmartGrid.idl")
set(IDL_SRC "${CMAKE_CURRENT_BINARY_DIR}/SmartGrid.c")
set(IDL_HDR "${CMAKE_CURRENT_BINARY_DIR}/SmartGrid.h")

# generate c/h files
add_custom_command(
    OUTPUT "${IDL_SRC}" "${IDL_HDR}"
    COMMAND ${IDL_COMPILER} "${IDL_FILE}"
    DEPENDS "${IDL_FILE}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    COMMENT "Generating C files from PowerData.idl"
)

add_library(PowerData_lib STATIC "${IDL_SRC}" "${IDL_HDR}")
target_include_directories(PowerData_lib PUBLIC "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(PowerData_lib PUBLIC CycloneDDS::ddsc)

add_executable(smartgrid smartgrid.c)

target_link_libraries(smartgrid PRIVATE PowerData_lib)


