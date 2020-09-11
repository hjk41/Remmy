cmake_minimum_required(VERSION 2.8.4)

project(SimpleRPC)

# communication layer
if (NOT COMM_LAYER)
    message(WARNING "Communication layer is not specified, using ASIO as default. Currently you can use ASIO or ZMQ.")
    set(COMM_LAYER "ASIO" CACHE STRING "Choose the type of communication layer." FORCE)
endif(NOT COMM_LAYER)

if(COMM_LAYER STREQUAL "ASIO")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DUSE_ASIO=1 -DASIO_STANDALONE=1")
elseif(COMM_LAYER STREQUAL "ZMQ")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DUSE_ZMQ=1")
else()
    message(FATAL_ERROR "Currently only ASIO and ZMQ are supported as communication layer.")
endif(COMM_LAYER STREQUAL "ASIO")

# check C++14
include(CheckCXXCompilerFlag)
CHECK_CXX_COMPILER_FLAG("-std=c++14" COMPILER_SUPPORTS_CXX14)
if(COMPILER_SUPPORTS_CXX14)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++14")
else()
    message(SEND_ERROR "The compiler ${CMAKE_CXX_COMPILER} has no C++14 support. Please use a different C++ compiler.")
endif()
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(test test/test.cpp)
target_include_directories(test PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(test pthread)

if(COMM_LAYER STREQUAL "ASIO")
    target_include_directories(test PUBLIC simple_rpc/asio/asio/include)
elseif(COMM_LAYER STREQUAL "ZMQ")
    target_include_directories(test PUBLIC simple_rpc/zmq)
    target_link_directories(test PUBLIC simple_rpc/zmq)
    target_link_libraries(test PUBLIC zmq)
else()
    message(FATAL_ERROR "Currently only ASIO and ZMQ are supported as communication layer.")
endif(COMM_LAYER STREQUAL "ASIO")
