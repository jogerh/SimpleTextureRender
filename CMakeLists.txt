cmake_minimum_required(VERSION 3.16)
project(SimpleTextureRender)
set(CMAKE_CXX_STANDARD 17)

add_executable(SimpleTextureRender main.cpp)
target_compile_options(SimpleTextureRender PUBLIC "-W4")