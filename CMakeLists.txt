cmake_minimum_required(VERSION 3.0)
project(Homework4 C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -pthread")

add_executable(Homework4 dmora_concurrency.c)
