#pragma once

#include <cstddef>
#include <string>
#include <string_view>

/**
 *  CMake 文本累积器：把已排好版的行按固定 4 空格缩进追加到内部缓冲，供 emitter 取只读视图落盘。
 *
 *  不变量：缓冲只由本类的方法追加；缩进级数非负，每级 kIndentWidth 个空格。
 *
 *  职责边界：只做缩进、换行与累积，不决定该不该写某行，不转义参数（参数转义归 Value/Argument.hpp 的
 *  render_argument），不做行尾归一化与行内容重排。多行文本由调用方拆成多次 push_line，本类不解析 '\n'。
 *
 *  离线工具、非运行时热路径：本模块不在逐帧或逐元素路径上，std::string / std::vector 的分配成本可接受，
 *  模型层因此不做逐点分配失败检查——这是对 cpp-style 第 9 节「分配失败不得忽略」的显式例外
 *  （依据：CMake 封装计划书第 1.2 节第 10 项，范围仅限容器与字符串分配）。
 */

namespace ID::CMake
{
    /**
     *  逐行累积 CMake 文本。缩进与顶格两种写法分开，避免调用方自行拼接前导空格。
     */
    class Writer
    {
    public:
        // 缩进单位：模板与生成文本统一 4 空格，不使用 Tab
        static inline constexpr std::size_t kIndentWidth = 4;

        /**
         *  @brief 按当前缩进写一行
         *  @param text 单行文本，不含 '\n'；空文本只写一个换行，不写前导空格
         *  @note 文本内部的前导空格原样保留，本类不合并、不重排、不剥除
         */
        void push_line(std::string_view text);

        /**
         *  @brief 顶格写一行，用于 `)`、`if`、`else`、`endif` 等必须从行首开始的片段
         *  @param text 单行文本，不含 '\n'；空文本只写一个换行
         */
        void push_unindented(std::string_view text);

        /**
         *  @brief 写一个空行，用于块间分组
         *  @note 空行不受缩进影响，恒写一个换行
         */
        void push_blank_line();

        /**
         *  @brief 进入一层缩进，后续 push_line 多写 kIndentWidth 个空格
         */
        void begin_scope();

        /**
         *  @brief 退出一层缩进
         *  @note 缩进为 0 时调用属调用方破坏内部不变量，用 ID_ASSERT 终止进程；
         *        该失败不进诊断通道，也不由 validate 判定（cpp-style 第 7、9 节）。
         */
        void end_scope();

        /**
         *  @brief 取得已累积文本的只读视图
         *  @return 指向内部缓冲的视图，生命周期跟随本对象的下一次追加或 reset()
         *  @note 只读；调用方不得保存超出上述生命周期的指针
         */
        std::string_view view() const noexcept;

        /**
         *  @brief 已累积文本的字节数
         */
        std::size_t size() const noexcept;

        /**
         *  @brief 清空缓冲并把缩进级数归零
         */
        void reset();

    private:
        std::string m_buffer = {};      // 累积文本，每行以 '\n' 结尾
        std::size_t m_indent = 0;       // 当前缩进级数，每级 kIndentWidth 个空格
    };
} // namespace ID::CMake
