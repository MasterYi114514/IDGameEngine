# vendor GLFW 3.5.1（裁剪副本）

## 来源

- 上游项目：GLFW 3.5.1（<https://www.glfw.org/>），本目录是上游源码树的裁剪副本。
- 许可：`LICENSE.md`（zlib 许可）与 `CONTRIBUTORS.md` 逐字保留，未做修改。
- 上游文档 `README.md` 与公开头 `include/GLFW/glfw3.h`、`include/GLFW/glfw3native.h` 同样逐字保留：
  消费者看到的 GLFW 接口语义以这些文件为准，本文件不转抄。

## 本副本的定位

本副本只作为 `IDPlugin/IDWindow` 的子目录接入，产出一个 `glfw` **静态库**：

- 支持平台：Windows（Win32/WGL）、macOS（Cocoa/NSGL）、Linux（X11，Wayland 可选）。
- 无库形态开关：不构建共享库，不定义 `_GLFW_BUILD_DLL`，不生成 `.dll`/`.so`/`.dylib` 与导入库。
- 无安装与打包面：不生成 `install`、`uninstall`、`export`、pkg-config 文件与 CMake 包配置文件。
- 独立 configure 直接失败：GLFW 必须由 IDWindow 以 `add_subdirectory` 接入，平台判断与依赖解析
  由上层负责（见 `IDPlugin/IDWindow/CMakeLists.txt`）。

## 删除清单

相对上游源码树删除了以下内容（均为文档、示例、测试、打包与工具资产，不参与 `glfw` 库构建）：

- `docs/`、`examples/`、`tests/`、`CMake/` 整目录。
- `deps/` 下 `glad/`、`getopt.c/.h`、`linmath.h`、`nuklear.h`、`nuklear_glfw_gl2.h`、
  `stb_image_write.h`、`tinycthread.c/.h`；只保留 `deps/wayland/` 的 9 个协议 XML
  （Wayland 后端在配置期用它们生成协议头与代码）。
- `src/glfw.rc.in`（仅 Windows 共享库版本的资源脚本）、`src/mappings.h.in`（`update_mappings`
  目标的输入；生成结果 `src/mappings.h` 已在上游源码树内，保留）。

保留下来的非构建资产：`include/GLFW/` 两个公开头、`LICENSE.md`、`CONTRIBUTORS.md`、`README.md`、
`deps/wayland/*.xml`，以及三平台全部后端源码（含 macOS 的 `cocoa_*.h/.m`、`nsgl_context.m`、
`macos_time.c/.h`）。

## 与上游 CMake 的差异

顶层 `CMakeLists.txt`：

- 删除 `GLFW_STANDALONE`、`BUILD_SHARED_LIBS`、`GLFW_BUILD_EXAMPLES`、`GLFW_BUILD_TESTS`、
  `GLFW_BUILD_DOCS`、`GLFW_INSTALL`、`GLFW_LIBRARY_TYPE`、`GLFW_USE_HYBRID_HPG`、
  `USE_MSVC_RUNTIME_LIBRARY_DLL` 等开关，以及 `GLFW_USE_OSMESA` / `GLFW_USE_WAYLAND` 的陷阱分支。
- 删除 `GNUInstallDirs`、`CMakePackageConfigHelpers`、`USE_FOLDERS`、包配置文件生成与全部 install 规则。
- 平台分派写死：Windows → Win32，macOS → Cocoa，Linux → X11（Wayland 由 `GLFW_BUILD_WAYLAND`
  选择，默认 `OFF`）；其余平台在配置期 `FATAL_ERROR`。
- 新增两处配置期校验：`glfw3.h` 的 `GLFW_VERSION_*` 必须与 `project()` 版本一致；
  `glfw` 目标已存在时报错（保证只接入一次）。
- CRT 继承产品：不再改写 `CMAKE_MSVC_RUNTIME_LIBRARY`，与同一构建树中其它目标保持同一运行库。
- 顶层不再按后端打印 `Including ... support` 状态行。

`src/CMakeLists.txt`：

- `add_library(glfw STATIC ...)` 写死静态库；删除 `GLFW_LIBRARY_TYPE` / `GLFW_BUILD_SHARED_LIBRARY`
  / `_GLFW_BUILD_DLL` / `OUTPUT_NAME` / `VERSION` / `SOVERSION` / `FOLDER` 与 MinGW 共享库加固链接项。
- 删除 `update_mappings` 目标（依赖已删除的 `CMake/GenerateMappings.cmake` 与 `mappings.h.in`）。
- 删除 `find_package(EpollShim)` 分支：非 Linux 的 Unix 平台已在顶层被拒绝，该分支不可达。
- 删除 pkg-config 变量聚合（`glfw_PKG_DEPS` / `glfw_PKG_LIBS`）与 `configure_file(glfw3.pc.in)`。
- 平台依赖改为 `INTERFACE`：Windows 的 `gdi32`、macOS 的 `Cocoa`/`IOKit`/`QuartzCore`/`CoreFoundation`。
  静态存档不记录这些符号，最终消费者（IDWindow 或其消费者）在链接期补上。
- `-fvisibility=hidden` 常开（`UNIX AND NOT MSVC`）：静态编入 IDWindow 的 `.so`/`.dylib` 时
  GLFW 符号不进入该共享库的导出面。
- 保留：三平台源清单、X11 六个扩展头的存在性检查与安装包提示、Wayland 的 `memfd_create` 探测、
  `wayland-scanner` 协议生成与 `pkg_check_modules`、`rt`/`m`/`CMAKE_DL_LIBS` 探测、
  `-Wall`（GNU/Clang/AppleClang）与 `/W3`（MSVC）、`_CRT_SECURE_NO_WARNINGS`、`_DEFAULT_SOURCE`、
  `POSITION_INDEPENDENT_CODE`、`C_STANDARD 99`、`C_EXTENSIONS OFF`、`Threads::Threads`。
  Linux 分支的 `pkg_check_modules`（`Wayland_*`）与 `check_function_exists`（`HAVE_MEMFD_CREATE`）、
  `find_program`（`WAYLAND_SCANNER_EXECUTABLE`）沿用上游变量名，只在 Linux 且
  `GLFW_BUILD_WAYLAND=ON` 时产生缓存条目。

## 升级方式

1. 取上游同一 3.5.x 版本的源码树（`LICENSE.md`、`CONTRIBUTORS.md`、`README.md`、`include/GLFW/`
   必须与上游一致）。
2. 按上面的删除清单裁剪目录与文件。
3. 按本文件与两份 `CMakeLists.txt` 的现状重做平台分派、源清单与依赖接线；顶层只保留
   `project()` 版本、平台分派、`glfw3.h` 版本校验、`find_package(Threads)` 与 `add_subdirectory(src)`。
4. 若上游版本号变化，同步改 `project()` 的版本（`glfw3.h` 校验会强制两者一致）。
5. 升级后按 `IDPlugin/IDWindow/README.md` 的验证状态节重新构建验证；未验证的平台分支如实标注。
