cmake_minimum_required(VERSION 3.10)

project(cpp-todo-server VERSION 1.0)

#设置 cpp 标准
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON) 

#提供头文件路径
include_directories(${CMAKE_SOURCE_DIR}/include)

#编译
add_executable(server
    src/todo-server.cpp
    src/router.cpp
    src/ThreadPool.cpp)


#链接第三方库
target_link_libraries(server PRIVATE pthread mysqlclient spdlog fmt)