#pragma once

#include <cstdint>
#include <string>
#include <string_view>

/**
 *  CMake 参数片段的值类型：四种类别决定渲染方式，渲染与裸值字符校验是与数据并列的自由函数。
 *
 *  Argument 是数据载体，构造期不抛异常、不校验字符：裸值的字符集判定由 is_valid_bare_argument 提供，
 *  由生成前校验在需要时报 InvalidBareArgument；这样"合不合法"只有一处判定，校验失败走诊断通道，
 *  不在构造点分散处理，也不让构造路径承担终止语义（cpp-style 第 12 节：数据载体不带逻辑）。
 *
 *  本层不求值变量、不展开列表、不解析路径：文本按原样承载，只在渲染时加引号与转义。
 *
 *  离线工具、非运行时热路径：本模块不在逐帧或逐元素路径上，std::string / std::vector 的分配成本可接受，
 *  模型层因此不做逐点分配失败检查——这是对 cpp-style 第 9 节「分配失败不得忽略」的显式例外
 *  （依据：CMake 封装计划书第 1.2 节第 10 项，范围仅限容器与字符串分配）。
 */

namespace ID::CMake
{
    /**
     *  参数片段的类别：取值语义由渲染决定，不携带取值范围信息。
     */
    enum class ArgumentKind : std::uint8_t
    {
        Bare         = 0,   // 裸字面量：IDWindow、PRIVATE、cxx_std_20
        Quoted       = 1,   // 加引号、禁止展开：字面路径，或被当作字面量的 '$'
        QuotedExpand = 2,   // 加引号、允许展开：${...}、$<...>、含空格的复合路径
        Verbatim     = 3,   // 原样透传：只用于命令与条件等原始词法片段
    };

    /**
     *  一段 CMake 参数文本及其类别。文本按值持有，副本彼此独立。
     */
    class Argument
    {
    public:
        /**
         *  @brief 构造裸值片段
         *  @param text 参数文本，按原样持有
         *  @return 类别为 Bare 的片段；合法性不在此判定，用 is_valid_bare_argument 判定
         */
        static Argument bare(std::string_view text);

        /**
         *  @brief 构造加引号且禁止展开的片段
         *  @param text 参数文本，按原样持有；允许为空（空引号是合法 CMake 参数）
         *  @return 类别为 Quoted 的片段
         */
        static Argument quoted(std::string_view text);

        /**
         *  @brief 构造加引号且保留变量展开的片段
         *  @param text 参数文本，按原样持有；允许为空
         *  @return 类别为 QuotedExpand 的片段
         */
        static Argument quoted_expand(std::string_view text);

        /**
         *  @brief 构造原样透传的片段
         *  @param text 原始词法片段，按原样持有
         *  @return 类别为 Verbatim 的片段
         *  @note 只允许出现在原始词法片段位置（条件、配置块命令、尾部命令）；
         *        出现在条目参数值位置由生成前校验报 VerbatimArgument
         */
        static Argument verbatim(std::string_view text);

        /**
         *  @brief 取得片段类别
         */
        ArgumentKind get_kind() const noexcept;

        /**
         *  @brief 取得渲染前的原始文本
         *  @return 指向本对象内部存储的视图，生命周期跟随本对象
         *  @note 只读；本对象被移动或销毁后视图失效
         */
        std::string_view get_text() const noexcept;

    private:
        Argument(std::string_view text, ArgumentKind kind);

        std::string  m_text = {};                     // 渲染前的原始文本
        ArgumentKind m_kind = ArgumentKind::Bare;     // 片段类别，由静态工厂在构造期决定
    };

    /**
     *  @brief 判定裸值片段是否可直接写出
     *  @param argument 待判定的片段
     *  @return Bare 且文本非空、字符全部落在 `A-Z a-z 0-9 _ - . / : + = @ ,` 内时为 true；
     *          Bare 且为空串或含字符集外字符（空格、中文、`$`、引号等）时为 false；
     *          Quoted / QuotedExpand / Verbatim 一律返回 true（本函数只服务裸值，调用方按类别决定是否调用）
     *  @note 纯函数，不分配内存、不缓存；不复用于注释头取值，注释头走单独判定
     */
    bool is_valid_bare_argument(const Argument& argument) noexcept;

    /**
     *  @brief 按类别把片段渲染成可直接写进 CMake 文本的参数
     *  @param argument 待渲染的片段
     *  @return Bare 原样；Quoted 两端加 `"` 并转义 `\`、`"`、`$`；QuotedExpand 两端加 `"` 并只转义 `\`、`"`；
     *          Verbatim 原样（不加引号、不转义）
     *  @note 渲染不改变文本承载的取值，也不判断片段是否可写在该位置；位置合法性由生成前校验判定
     */
    std::string render_argument(const Argument& argument);
} // namespace ID::CMake
