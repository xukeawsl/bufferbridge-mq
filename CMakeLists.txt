cmake_minimum_required(VERSION 3.16)

project(bufferbridge-mq)

option(LINK_SO "Whether examples are linked dynamically" OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_compile_options(-Wno-deprecated-declarations)

list(APPEND CMAKE_PREFIX_PATH /usr/local/3rd/protobuf-3.19.4)
list(APPEND CMAKE_PREFIX_PATH /usr/local/3rd/openssl-1.1.1q)
list(APPEND CMAKE_PREFIX_PATH /usr/local/3rd/brpc-1.15.0)

find_package(Protobuf REQUIRED)
find_package(OpenSSL REQUIRED)

add_subdirectory(third-party/rocketmq-client-cpp-5.0.3)
add_subdirectory(third-party/yaml-cpp-0.8.0)

file(GLOB_RECURSE SRC_FILES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
file(GLOB_RECURSE HDR_FILES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h")

find_path(BRPC_INCLUDE_PATH NAMES brpc/server.h)
if(LINK_SO)
    find_library(BRPC_LIB NAMES brpc)
else()
    find_library(BRPC_LIB NAMES libbrpc.a brpc)
endif()
if((NOT BRPC_INCLUDE_PATH) OR (NOT BRPC_LIB))
    message(FATAL_ERROR "Fail to find brpc")
endif()

find_path(LEVELDB_INCLUDE_PATH NAMES leveldb/db.h)
find_library(LEVELDB_LIB NAMES leveldb)
if ((NOT LEVELDB_INCLUDE_PATH) OR (NOT LEVELDB_LIB))
    message(FATAL_ERROR "Fail to find leveldb")
endif()

add_executable(bufferbridge-mq main.cpp ${SRC_FILES})

target_include_directories(bufferbridge-mq PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${BRPC_INCLUDE_PATH}
    ${LEVELDB_INCLUDE_PATH}
    ${OPENSSL_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/third-party/hot-loader
    ${CMAKE_CURRENT_SOURCE_DIR}/third-party/json-3.12.0/single_include
)

target_link_directories(bufferbridge-mq PRIVATE
    /usr/local/3rd/brpc-1.15.0/lib
)

target_link_libraries(bufferbridge-mq PRIVATE
    api 
    yaml-cpp::yaml-cpp
    ${BRPC_LIB}
    gflags
    pthread
    rocketmq
    ${PROTOBUF_LIBRARIES}
    ${LEVELDB_LIB}
    ${OPENSSL_CRYPTO_LIBRARY}
    ${OPENSSL_SSL_LIBRARY}
    dl
)

#
# Clang-Format
#
find_program(CLANG_FORMAT_EXECUTABLE
    NAME "clang-format-12"
    PATHS "/usr/bin" "/usr/local/bin"
)

if (CLANG_FORMAT_EXECUTABLE)
    message(STATUS "Successfully find program `clang-format-12`")
    message(STATUS "You can use the `make clang-format` command to automatically format the code style")
    add_custom_target(clang-format
    COMMAND
        ${CLANG_FORMAT_EXECUTABLE} --style=file -i ${HDR_FILES};${SRC_FILES};${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
    COMMENT
        "Automatically format the code style"
    )
endif()