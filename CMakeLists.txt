# 该工具用来对Elf文件符号表、CRC等进行魔改适配
# 用于在特性场景下已编译的可执行程序、动态库、驱动不兼容问题
# ！！！！！！！！！！非专业人员谨慎使用！！！！！！！！！！
project(cadapt)
cmake_minimum_required(VERSION 2.8)

include_directories(.)
FILE(GLOB SRC_LIST
    "./*.h"
    "./*.cpp"
    "./kmod/*.h"
    "./kmod/*.cpp"
)

#set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=gnu++11  -static-libgcc -static-libstdc++")

add_executable(${PROJECT_NAME} ${SRC_LIST})
