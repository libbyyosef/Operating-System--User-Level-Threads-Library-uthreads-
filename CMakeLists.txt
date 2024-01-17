cmake_minimum_required(VERSION 3.16)
project(os_ex2_libby)

set(CMAKE_CXX_STANDARD 14)

include_directories(.)

add_executable(os_ex2_libby
        uthreads.cpp
        uthreads.h)
