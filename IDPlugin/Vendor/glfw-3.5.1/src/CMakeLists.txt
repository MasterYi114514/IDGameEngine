# GLFW 静态库：本副本没有任何库形态开关，SHARED 形态由 IDWindow 决定是否包一层，
# 因此这里不生成导出符号，也不为归档写版本号与符号版本（静态归档不承载这些信息）。
# 源文件显式列出而不是 GLOB：漏编只在链接期以未定义符号暴露，新增源必须同步改这里。

add_library(glfw STATIC
    "${GLFW_SOURCE_DIR}/include/GLFW/glfw3.h"
    "${GLFW_SOURCE_DIR}/include/GLFW/glfw3native.h"
    internal.h
    platform.h
    mappings.h
    context.c
    init.c
    input.c
    monitor.c
    platform.c
    vulkan.c
    window.c
    egl_context.c
    osmesa_context.c
    null_platform.h
    null_joystick.h
    null_init.c
    null_monitor.c
    null_window.c
    null_joystick.c
)

# 时间、线程与模块装载按操作系统共享（null 后端也要用），与后端选择无关
if(WIN32)
    target_sources(glfw PRIVATE
        win32_platform.h
        win32_joystick.h
        win32_time.h
        win32_thread.h
        win32_init.c
        win32_joystick.c
        win32_monitor.c
        win32_window.c
        win32_module.c
        win32_time.c
        win32_thread.c
        wgl_context.c
    )

    target_compile_definitions(glfw PRIVATE _GLFW_WIN32 UNICODE _UNICODE)

    # GLFW 的窗口与监视器路径直接调 GDI（CreateDCW/GetDC/GetDeviceCaps）：静态存档不记录
    # 这些符号，链接期必须由最终消费者补 gdi32，所以是 INTERFACE 而不是 PRIVATE
    target_link_libraries(glfw INTERFACE gdi32)
elseif(APPLE)
    enable_language(OBJC)

    target_sources(glfw PRIVATE
        cocoa_platform.h
        cocoa_joystick.h
        cocoa_init.m
        cocoa_joystick.m
        cocoa_monitor.m
        cocoa_window.m
        nsgl_context.m
        macos_time.h
        macos_time.c
        posix_thread.h
        posix_module.c
        posix_thread.c
    )

    target_compile_definitions(glfw PRIVATE _GLFW_COCOA)

    # 静态存档不承载框架依赖，同理交给最终消费者
    target_link_libraries(glfw INTERFACE
        "-framework Cocoa"
        "-framework IOKit"
        "-framework QuartzCore"
        "-framework CoreFoundation"
    )
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(glfw PRIVATE
        x11_platform.h
        x11_init.c
        x11_monitor.c
        x11_window.c
        xkb_unicode.c
        glx_context.c
        linux_joystick.h
        linux_joystick.c
        posix_poll.h
        posix_poll.c
        posix_time.h
        posix_thread.h
        posix_module.c
        posix_time.c
        posix_thread.c
    )

    # X11 由 GLFW 在运行期经 dlopen 装载：配置期只需要头文件，链接期只需要 libdl
    target_compile_definitions(glfw PRIVATE _GLFW_X11)

    # -std=c99 会关掉 _DEFAULT_SOURCE，而 GLFW 的 POSIX 路径需要它（POSIX 2008 及更高）
    target_compile_definitions(glfw PRIVATE _DEFAULT_SOURCE)

    find_package(X11 REQUIRED)
    target_include_directories(glfw PRIVATE "${X11_X11_INCLUDE_PATH}")

    # Check for XRandR (modern resolution switching and gamma control)
    if(NOT X11_Xrandr_INCLUDE_PATH)
        message(FATAL_ERROR "RandR headers not found; install libxrandr development package")
    endif()
    target_include_directories(glfw PRIVATE "${X11_Xrandr_INCLUDE_PATH}")

    # Check for Xinerama (legacy multi-monitor support)
    if(NOT X11_Xinerama_INCLUDE_PATH)
        message(FATAL_ERROR "Xinerama headers not found; install libxinerama development package")
    endif()
    target_include_directories(glfw PRIVATE "${X11_Xinerama_INCLUDE_PATH}")

    # Check for Xkb (X keyboard extension)
    if(NOT X11_Xkb_INCLUDE_PATH)
        message(FATAL_ERROR "XKB headers not found; install X11 development package")
    endif()
    target_include_directories(glfw PRIVATE "${X11_Xkb_INCLUDE_PATH}")

    # Check for Xcursor (cursor creation from RGBA images)
    if(NOT X11_Xcursor_INCLUDE_PATH)
        message(FATAL_ERROR "Xcursor headers not found; install libxcursor development package")
    endif()
    target_include_directories(glfw PRIVATE "${X11_Xcursor_INCLUDE_PATH}")

    # Check for XInput (modern HID input)
    if(NOT X11_Xi_INCLUDE_PATH)
        message(FATAL_ERROR "XInput headers not found; install libxi development package")
    endif()
    target_include_directories(glfw PRIVATE "${X11_Xi_INCLUDE_PATH}")

    # Check for X Shape (custom window input shape)
    if(NOT X11_Xshape_INCLUDE_PATH)
        message(FATAL_ERROR "X Shape headers not found; install libxext development package")
    endif()
    target_include_directories(glfw PRIVATE "${X11_Xshape_INCLUDE_PATH}")

    if(GLFW_BUILD_WAYLAND)
        target_compile_definitions(glfw PRIVATE _GLFW_WAYLAND)

        target_sources(glfw PRIVATE
            wl_platform.h
            wl_init.c
            wl_monitor.c
            wl_window.c
        )

        # wayland-scanner 生成的协议头落在本目录的二进制目录下，wl_*.c 直接按名包含
        target_include_directories(glfw PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

        include(CheckIncludeFiles)
        include(CheckFunctionExists)
        check_function_exists(memfd_create HAVE_MEMFD_CREATE)
        if(HAVE_MEMFD_CREATE)
            target_compile_definitions(glfw PRIVATE HAVE_MEMFD_CREATE)
        endif()

        find_program(WAYLAND_SCANNER_EXECUTABLE NAMES wayland-scanner)
        if(NOT WAYLAND_SCANNER_EXECUTABLE)
            message(FATAL_ERROR "Failed to find wayland-scanner")
        endif()

        macro(generate_wayland_protocol protocol_file)
            set(protocol_path "${CMAKE_CURRENT_SOURCE_DIR}/../deps/wayland/${protocol_file}")

            string(REGEX REPLACE "\\.xml$" "-client-protocol.h" header_file ${protocol_file})
            string(REGEX REPLACE "\\.xml$" "-client-protocol-code.h" code_file ${protocol_file})

            add_custom_command(OUTPUT ${header_file}
                COMMAND "${WAYLAND_SCANNER_EXECUTABLE}" client-header "${protocol_path}" ${header_file}
                DEPENDS "${protocol_path}"
                VERBATIM)

            add_custom_command(OUTPUT ${code_file}
                COMMAND "${WAYLAND_SCANNER_EXECUTABLE}" private-code "${protocol_path}" ${code_file}
                DEPENDS "${protocol_path}"
                VERBATIM)

            target_sources(glfw PRIVATE ${header_file} ${code_file})
        endmacro()

        generate_wayland_protocol("wayland.xml")
        generate_wayland_protocol("viewporter.xml")
        generate_wayland_protocol("xdg-shell.xml")
        generate_wayland_protocol("idle-inhibit-unstable-v1.xml")
        generate_wayland_protocol("pointer-constraints-unstable-v1.xml")
        generate_wayland_protocol("relative-pointer-unstable-v1.xml")
        generate_wayland_protocol("fractional-scale-v1.xml")
        generate_wayland_protocol("xdg-activation-v1.xml")
        generate_wayland_protocol("xdg-decoration-unstable-v1.xml")

        include(FindPkgConfig)

        pkg_check_modules(Wayland REQUIRED
            wayland-client>=0.2.7
            wayland-cursor>=0.2.7
            wayland-egl>=0.2.7
            xkbcommon>=0.5.0)

        target_include_directories(glfw PRIVATE ${Wayland_INCLUDE_DIRS})
    endif()

    # Wayland/X11 客户端库同样是运行期装载，这里只需要它的依赖探测结果
    find_library(RT_LIBRARY rt)
    mark_as_advanced(RT_LIBRARY)
    if(RT_LIBRARY)
        target_link_libraries(glfw PRIVATE "${RT_LIBRARY}")
    endif()

    find_library(MATH_LIBRARY m)
    mark_as_advanced(MATH_LIBRARY)
    if(MATH_LIBRARY)
        target_link_libraries(glfw PRIVATE "${MATH_LIBRARY}")
    endif()

    if(CMAKE_DL_LIBS)
        target_link_libraries(glfw PRIVATE "${CMAKE_DL_LIBS}")
    endif()
endif()

# 静态库也可能被编进 IDWindow 的 SHARED 形态，位置无关是硬要求
set_target_properties(glfw PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    C_STANDARD 99
    C_EXTENSIONS OFF
)

target_include_directories(glfw
    SYSTEM PUBLIC
        "$<BUILD_INTERFACE:${GLFW_SOURCE_DIR}/include>"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(glfw PRIVATE Threads::Threads)

# 警告沿用上游的常用集；不引入 -Werror，三方代码的既有警告不应阻塞产品构建
# NOTE: 顺序有意义，Clang-CL 同时匹配 MSVC 与 Clang 两个分支
if(MSVC)
    target_compile_options(glfw PRIVATE "/W3")
elseif(CMAKE_C_COMPILER_ID STREQUAL "GNU" OR
        CMAKE_C_COMPILER_ID STREQUAL "Clang" OR
        CMAKE_C_COMPILER_ID STREQUAL "AppleClang")
    target_compile_options(glfw PRIVATE "-Wall")
endif()

# MS CRT 把部分 C 运行库函数标记为已弃用
if(MSVC OR CMAKE_C_SIMULATE_ID STREQUAL "MSVC")
    target_compile_definitions(glfw PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# 静态编进 IDWindow 的 .so/.dylib 时不把 GLFW 符号暴露为该共享库的导出面
if(UNIX AND NOT MSVC)
    target_compile_options(glfw PRIVATE "-fvisibility=hidden")
endif()
