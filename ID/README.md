# ID 微内核

只做四件事：类型注册、自举日志、内存分配、插件管理。构建由根 `CMakeLists.txt` 与 preset 驱动，
本目录不维护输出路径开关。

## 输出目录

没有 `BIN_DIR` 之类的本目录变量：产物目录由根 `CMakeLists.txt` 统一设定，按构建目录与模式隔离。

- 内核与分配桩：`<build>/out/<shared|static>/bin`（`.dll`/`.exe`）与 `<build>/out/<shared|static>/lib`（`.a`/`.dll.a`）
- 组装层与选中插件：`<build>/Assemble/out/{bin,lib}`
- 构建元数据：`<build>/IDBuildInfo.txt`

多配置生成器会在上述路径后再追加 `<Config>/`。

## 构建

公共 preset 在 `CMakePresets.json`（`dev` / `dev-mono` / `ship`），本机生成器放在被忽略的
`CMakeUserPresets.json`（`dev-mingw` / `dev-mono-mingw` / `ship-mingw`）：

```powershell
cmake --preset dev
cmake --build build/dev --parallel
```

`dev` / `dev-mono` 会设置 `ID_ASSEMBLY_PROFILE` 与 `ID_BUILD_SANDBOX=ON`；不设置 profile 时只有
内核与分配桩可构建，显式要求宿主却没有 profile 会在配置期失败。

## 消费方式

内核是普通 CMake 目标 `ID`，公开头以 `BUILD_INTERFACE:${ID_DIR}/include` 暴露，安装树用
`INSTALL_INTERFACE`，因此消费方只需 `target_link_libraries(<target> PRIVATE ID)`，不必手动添加
`ID/include`。产品级消费请链接组装层提供的 `IDGameEngine`，不要直接链接内核（计划书 9.3）。
