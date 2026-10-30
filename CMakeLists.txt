cmake_minimum_required(VERSION 3.15)

project(IDGameEngine)

# MSVC 下统一源码字符集与执行字符集为 UTF-8：本仓库源码含中文注释与中文日志消息，
# 中文日志消息按 UTF-8 解释依赖此项。选项只作用于本仓库构建树；外部消费者若直接使用
# IDLog 公开头，需在自身构建中同样提供 /utf-8（该约束记录在 IDPlugin/IDLog/README.md）。
if(MSVC)
    add_compile_options(/utf-8)
endif()

add_subdirectory(IDPlugin)
add_subdirectory(Benchmark)