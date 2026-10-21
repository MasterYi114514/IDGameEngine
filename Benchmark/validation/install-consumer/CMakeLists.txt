cmake_minimum_required(VERSION 3.21)

# =============================================================================
# P5 安装消费校验（Benchmark 校验资产，不是生产目标）
#   - 独立工程：只通过 find_package(IDGameEngine) 解析安装前缀里的导出 target；
#   - 不 include 任何源码树路径，也不链接任何源码树目标：能配置成功即证明
#     安装导出（IDGameEngineConfig.cmake + IDGameEngineTargets*.cmake）可重定位；
#   - 最小程序只调用 ID::Engine::start()/shutdown()，不测性能。
# 用法：
#   cmake -S <本目录> -B <独立构建目录> -G "MinGW Makefiles" -DCMAKE_PREFIX_PATH=<安装前缀>
# =============================================================================
project(IDGameEngineInstallConsumer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(IDGameEngine REQUIRED)

add_executable(install_consumer main.cpp)
target_link_libraries(install_consumer PRIVATE IDGameEngine)

# 与工程内一致的编译基线（消费方自己的选择，不属于安装导出契约）
if(MSVC)
    target_compile_options(install_consumer PRIVATE /utf-8 /W4 /GR-)
else()
    target_compile_options(install_consumer PRIVATE -Wall -Wextra -fno-rtti -fno-exceptions)
endif()
