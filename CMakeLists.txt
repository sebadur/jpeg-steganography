cmake_minimum_required(VERSION 3.22.1)
project("jpeg steganography")
set(CMAKE_CXX_STANDARD 20)

add_executable(jpeg-steganography main.cpp)
target_link_libraries(jpeg-steganography jpeg)
