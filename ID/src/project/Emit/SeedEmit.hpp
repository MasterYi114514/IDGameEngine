#pragma once

#include <string_view>

#include "project/Emit/GeneratedTexts.hpp"

/**
 *  `src/main.cpp` 入口骨架的产出（docs/id/08 §8.7、spec §7.5）。
 *
 *  骨架不引用任何插件：入口一旦引用插件，删除该插件后项目就编不过，而工具不重写用户源码。
 *  内容不含插件名、不含中文、不含时间戳——避免不同代码页下的编码差异，也让"同一输入产出同一字节"成立。
 *
 *  是否写出由计划阶段判定：本函数**总是**产出内容，文件已存在时不覆盖、`build` 不重建都不在这里判断。
 */

namespace ID::Project
{
    /**
     *  @brief 产出 `src/main.cpp` 的入口骨架
     *  @param project_name 项目名，写进 `std::puts` 的字面量参数
     *  @return 目标相对路径 `src/main.cpp`、正文与整文件摘要；摘要取本次产出内容的字节摘要
     *  @note 项目名在 P1 已按 `^[A-Za-z][A-Za-z0-9_-]*$` 校验，因此可直接作为 C 字符串字面量的内容，
     *        不需要额外转义；这里不为非法名字补兜底分支
     */
    GeneratedText emit_seed(std::string_view project_name);
} // namespace ID::Project
