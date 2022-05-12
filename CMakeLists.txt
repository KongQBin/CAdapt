#该工具用来对Elf文件符号表与LIBC库中的符号表做适配
#前提是目标Elf文件中没有用到目标LIBC库中不存在的符号
project(GlibcAdapted)
cmake_minimum_required(VERSION 2.8)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS}  -static-libgcc -static-libstdc++")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=gnu++11  -static-libgcc -static-libstdc++")
aux_source_directory(. SRC_LIST)
add_executable(${PROJECT_NAME} ${SRC_LIST})
