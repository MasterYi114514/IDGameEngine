cmake_minimum_required(VERSION 3.21)

# =============================================================================
# 安装消费校验工程（Benchmark 校验资产，不是生产目标）
#
#   - 独立工程：只通过 find_package(IDGameEngine) 解析安装前缀里的导出 target；
#   - 不 include 任何源码树路径，也不链接任何源码树目标：能配置/构建/运行成功，
#     即证明安装导出（IDGameEngineConfig.cmake + IDGameEngineTargets*.cmake）可重定位；
#   - 驱动脚本 Benchmark/validation/install-consumer/RunInstallConsumerMatrix.ps1 负责
#     逐格给出安装前缀与期望值，本文件不做任何产品选择；
#   - 不写官方 report.txt，不参与评级。
#
# 用法（单格手工复现）：
#   cmake -S <本目录> -B <独立构建目录> -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release `
#         -DCMAKE_PREFIX_PATH=<安装前缀> -DID_CONSUMER_EXPECT_MODE=SHARED `
#         -DID_CONSUMER_EXPECT_ASSEMBLY=ship -DID_CONSUMER_EXPECT_IDLOG=ON `
#         -DID_CONSUMER_EXPECT_MATH_SCALAR=ON
# =============================================================================

project(IDGameEngineInstallConsumer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---- 顺序反例（默认关闭）-----------------------------------------------------
# `find_package(IDGameEngine)` 必须在创建消费 target **之前**调用：产品的静态 CRT 默认值
# （MSVC 的 CMAKE_MSVC_RUNTIME_LIBRARY）只是普通变量，只在 target 创建时被读取一次。
# 打开本选项会故意在 find_package 之前创建一个 target，用来证明下面的顺序自检确实能识别顺序错误。
option(ID_CONSUMER_DEMO_WRONG_ORDER "故意在 find_package 之前创建 target，用于证明顺序自检有效" OFF)
if(ID_CONSUMER_DEMO_WRONG_ORDER)
    add_executable(id_order_demo main.cpp)
endif()

# ---- 必需顺序：先 find_package，再创建消费 target ---------------------------
find_package(IDGameEngine REQUIRED)

if(ID_CONSUMER_DEMO_WRONG_ORDER)
    if(MSVC)
        get_target_property(id_order_demo_crt id_order_demo MSVC_RUNTIME_LIBRARY)
        if(NOT id_order_demo_crt)
            message(FATAL_ERROR
                "[order-check] 顺序错误被识别：target 在 find_package(IDGameEngine) 之前创建，"
                "没有拿到产品的静态 CRT 默认值（CMAKE_MSVC_RUNTIME_LIBRARY）。"
                "find_package 必须写在创建消费 target 之前。")
        endif()
        message(STATUS "[order-check] 反例未命中（不应发生）：${id_order_demo_crt}")
    else()
        message(STATUS "[order-check] 顺序反例只在 MSVC 上有判据（静态 CRT 默认值），当前工具链跳过")
    endif()
endif()

# ---- 产品元数据：安装前缀必须来自预期的那一个产品 ---------------------------
set(ID_CONSUMER_EXPECT_MODE     "" CACHE STRING "期望的产品模式：SHARED | STATIC")
set(ID_CONSUMER_EXPECT_ASSEMBLY "" CACHE STRING "期望的产品 assembly 名（profile 的 assembly_name）")

if(NOT ID_CONSUMER_EXPECT_MODE OR NOT ID_CONSUMER_EXPECT_ASSEMBLY)
    message(FATAL_ERROR "消费校验必须显式给 -DID_CONSUMER_EXPECT_MODE=<SHARED|STATIC> 与 -DID_CONSUMER_EXPECT_ASSEMBLY=<assembly 名>")
endif()
if(NOT IDGameEngine_PRODUCT_MODE STREQUAL ID_CONSUMER_EXPECT_MODE)
    message(FATAL_ERROR
        "安装前缀的产品模式是 ${IDGameEngine_PRODUCT_MODE}，期望 ${ID_CONSUMER_EXPECT_MODE}："
        "前缀与配置格不匹配（产品或前缀被复用）")
endif()
if(NOT IDGameEngine_PRODUCT_ASSEMBLY STREQUAL ID_CONSUMER_EXPECT_ASSEMBLY)
    message(FATAL_ERROR
        "安装前缀的 assembly 是 ${IDGameEngine_PRODUCT_ASSEMBLY}，期望 ${ID_CONSUMER_EXPECT_ASSEMBLY}")
endif()
if(NOT EXISTS "${IDGameEngine_PRODUCT_MANIFEST}")
    message(FATAL_ERROR "安装前缀缺少产品清单：${IDGameEngine_PRODUCT_MANIFEST}")
endif()

# ---- 编译期期望：与产品 profile 的选中集合逐项对应 ---------------------------
option(ID_CONSUMER_EXPECT_IDLOG       "产品按 profile 选中 ID.Log"       OFF)
option(ID_CONSUMER_EXPECT_MATH_SCALAR "产品按 profile 选中 ID.Math.Scalar" OFF)
option(ID_CONSUMER_EXPECT_MATH_SIMD   "产品按 profile 选中 ID.Math.SIMD"   OFF)

# A+B 产品必须由消费方显式选一个 backend；空值表示"产品只有一个数学 backend"或"没有数学 backend"
set(ID_CONSUMER_MATH_BACKEND "" CACHE STRING "A+B 产品的编译期选择：scalar | simd")

add_executable(install_consumer main.cpp)
target_link_libraries(install_consumer PRIVATE IDGameEngine)

if(ID_CONSUMER_EXPECT_IDLOG)
    target_compile_definitions(install_consumer PRIVATE ID_CONSUMER_EXPECT_IDLOG=1)
endif()
if(ID_CONSUMER_EXPECT_MATH_SCALAR)
    target_compile_definitions(install_consumer PRIVATE ID_CONSUMER_EXPECT_MATH_SCALAR=1)
endif()
if(ID_CONSUMER_EXPECT_MATH_SIMD)
    target_compile_definitions(install_consumer PRIVATE ID_CONSUMER_EXPECT_MATH_SIMD=1)
endif()

if(ID_CONSUMER_MATH_BACKEND STREQUAL "scalar")
    target_compile_definitions(install_consumer PRIVATE ID_CONSUMER_MATH_BACKEND_SCALAR=1)
elseif(ID_CONSUMER_MATH_BACKEND STREQUAL "simd")
    target_compile_definitions(install_consumer PRIVATE ID_CONSUMER_MATH_BACKEND_SIMD=1)
elseif(NOT ID_CONSUMER_MATH_BACKEND STREQUAL "")
    message(FATAL_ERROR "ID_CONSUMER_MATH_BACKEND 只能是 scalar / simd / 空，收到：${ID_CONSUMER_MATH_BACKEND}")
endif()

# ---- 顺序自检：静态 CRT 默认值必须真的落在本 target 上 -----------------------
# 这一条同时是"find_package 写在创建 target 之前"的可自动识别判据：顺序写错时属性为空。
if(MSVC)
    get_target_property(id_consumer_crt install_consumer MSVC_RUNTIME_LIBRARY)
    if(NOT id_consumer_crt)
        message(FATAL_ERROR
            "消费 target 没有拿到静态 CRT 默认值：find_package(IDGameEngine) 必须在创建 target 之前调用")
    endif()
    message(STATUS "install-consumer: MSVC_RUNTIME_LIBRARY=${id_consumer_crt}")
endif()

# ---- 与工程内一致的编译基线（消费方自己的选择，不属于安装导出契约）----------
if(MSVC)
    target_compile_options(install_consumer PRIVATE /utf-8 /W4 /GR- /EHs-c-)
else()
    target_compile_options(install_consumer PRIVATE -Wall -Wextra -fno-rtti -fno-exceptions)
endif()
