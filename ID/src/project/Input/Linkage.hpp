#pragma once

#include <cstdint>
#include <string_view>

/**
 *  两处共用的形态词：项目选择里的 `linkage_type` 与插件清单里的 `requirements`。
 *
 *  不变量：
 *  1. 形态只用 Linkage 表达，`requirements` 的作者约束只用 Requirement 表达；两者不互相替代，
 *     项目选择与作者约束的相容性判定由 Resolve/EffectiveSelection 负责。
 *  2. `text` 为 `plugins.json` 与 `resolved` 记录里的唯一取值形态（小写 `static`/`dynamic`），
 *     `requirement_text` 为清单与记录里的唯一取值形态（大写 `STATIC_ONLY`/`SHARED_ONLY`/空）。
 *  3. 大小写敏感：只接受上面列出的字面值，不做大小写折叠与别名接受。
 */

namespace ID::Project
{
    /**
     *  链接形态：映射 CMake 的 STATIC / SHARED。
     */
    enum class Linkage : std::uint8_t
    {
        Static  = 0,   // STATIC
        Dynamic = 1,   // SHARED
    };

    /**
     *  清单 `requirements` 的作者约束。空数组 = 两种形态皆可且缺省 Static。
     */
    enum class Requirement : std::uint8_t
    {
        Any        = 0,   // 空 requirements
        StaticOnly = 1,   // 用户只能选 static
        SharedOnly = 2,   // 用户只能选 dynamic
    };

    /**
     *  @brief 形态的文本形式
     *  @param linkage 形态
     *  @return 静态存储的 "static" / "dynamic"
     */
    inline std::string_view to_string(Linkage linkage) noexcept
    {
        return linkage == Linkage::Dynamic ? std::string_view("dynamic") : std::string_view("static");
    }

    /**
     *  @brief 解析形态文本
     *  @param text 待解析文本，大小写敏感
     *  @param out 解析结果；成功时被覆盖，失败时保持原值
     *  @return 恰为 "static" 或 "dynamic" 时返回 true
     *  @note 出参形态沿用 docs/id/02 §2.3 的签名；失败语义是"保持原值"，调用方据返回值判断
     */
    inline bool parse_linkage(std::string_view text, Linkage& out) noexcept
    {
        if (text == "static")
        {
            out = Linkage::Static;
            return true;
        }
        if (text == "dynamic")
        {
            out = Linkage::Dynamic;
            return true;
        }
        return false;
    }

    /**
     *  @brief 作者约束的文本形式
     *  @param requirement 作者约束
     *  @return "STATIC_ONLY" / "SHARED_ONLY" / 空串（Requirement::Any）
     */
    inline std::string_view requirement_text(Requirement requirement) noexcept
    {
        if (requirement == Requirement::StaticOnly)
        {
            return std::string_view("STATIC_ONLY");
        }
        if (requirement == Requirement::SharedOnly)
        {
            return std::string_view("SHARED_ONLY");
        }
        return std::string_view{};
    }

    /**
     *  @brief 解析 `requirements` 的元素文本
     *  @param text 待解析文本，大小写敏感
     *  @param out 解析结果；成功时被覆盖，失败时保持原值
     *  @return 恰为 "STATIC_ONLY" 或 "SHARED_ONLY" 时返回 true
     */
    inline bool parse_requirement(std::string_view text, Requirement& out) noexcept
    {
        if (text == "STATIC_ONLY")
        {
            out = Requirement::StaticOnly;
            return true;
        }
        if (text == "SHARED_ONLY")
        {
            out = Requirement::SharedOnly;
            return true;
        }
        return false;
    }

    /**
     *  @brief 由作者约束推出闭包补齐项的缺省形态
     *  @param requirement 作者约束
     *  @return StaticOnly 与 Any 返回 Static，SharedOnly 返回 Dynamic
     *  @note 只用于闭包补齐项；直接选择项一律取 `plugins.json` 的 `linkage_type`
     */
    inline Linkage default_linkage(Requirement requirement) noexcept
    {
        return requirement == Requirement::SharedOnly ? Linkage::Dynamic : Linkage::Static;
    }
} // namespace ID::Project
