#include "project/Emit/SeedEmit.hpp"

#include <string>
#include <string_view>

#include "project/Base/Digest.hpp"

/**
 *  入口骨架：固定五行 C++，只把项目名放进 `std::puts` 的参数。
 *
 *  文本确定性的来源：不读磁盘、不读清单、不含时间戳与本机路径；项目名由参数给出，
 *  且已在 P1 按项目名字符规则校验（只含 ASCII 字母、数字、下划线与连字符）。
 *
 *  摘要按整文件字节计算：manifest 里 `seed` 条目只记录首次创建时的摘要，之后不参与校验、不覆盖、不删除
 *  （判断"是不是首次创建"是计划阶段的事，本单元只给出本次内容的摘要）。
 */

namespace ID::Project
{
    namespace
    {
        // 入口文件的固定目标路径（相对项目根）
        constexpr std::string_view kSeedFilePath = "src/main.cpp";

        // 骨架文本：不含插件名、不含中文、不含时间戳；行尾统一 '\n'
        std::string make_seed_text(std::string_view project_name)
        {
            std::string text("#include <cstdio>\n");
            text.append("\n");
            text.append("int main()\n");
            text.append("{\n");
            text.append("    std::puts(\"");
            text.append(project_name);
            text.append("\");\n");
            text.append("    return 0;\n");
            text.append("}\n");
            return text;
        }
    } // namespace

    GeneratedText emit_seed(std::string_view project_name)
    {
        GeneratedText text;
        text.target_relative = std::string(kSeedFilePath);
        text.text            = make_seed_text(project_name);
        text.digest          = digest_text(text.text);
        return text;
    }
} // namespace ID::Project
