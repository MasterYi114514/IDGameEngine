#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <vector>

#include "project/Base/Error.hpp"

/**
 *  诊断的五要素载体、诊断集合与退出码选择。
 *
 *  本头只持数据与排序/选择机制，不做业务判定：谁该产出哪条诊断、哪条属于 warn、一次运行算不算
 *  有差异，都由产出单元（U2–U9）决定；本头只保证"多类别同时存在时取唯一退出码"和"输出顺序确定"。
 *
 *  不变量：
 *  1. 一条诊断的五要素是「类别、对象、期望、实际、下一步」；定位单列在 location，不占五要素之一。
 *  2. 是否 warn 只由类别决定（is_warning），不设第二个布尔位，避免"类别说 1、标记说 warn"的两份真源。
 *  3. 追加顺序即发现顺序；输出前取 sorted() 的稳定顺序，同一输入重复运行得到同一顺序。
 *  4. 退出码只有一个入口 select_exit_code；差异码 6 不是诊断类别，只由 has_plan_diff 表达。
 *
 *  离线工具、非运行时热路径：本模块不在逐帧或逐元素路径上，std::string / std::vector 的分配成本可接受，
 *  因此不做逐点分配失败检查——这是对 cpp-style 第 9 节「分配失败不得忽略」的显式例外（范围仅限容器与
 *  字符串分配），与 cmake/Common 的既有口径一致。
 */

namespace ID::Project
{
    /**
     *  单条诊断：类别、对象、期望、实际、定位与下一步动作（spec §13.2 末条）。
     *  数据载体不带判定，只在产出点填值；ProjectCategory 没有表成功的取值可当默认值，
     *  因此 category 不设默认成员初始化器——产出点用聚合初始化一次给全六项。
     */
    struct ProjectDiagnostic
    {
        ProjectCategory category;       // 类别
        std::string     subject;        // 对象：项目名、插件名、条目身份或命令
        std::string     expected;       // 期望
        std::string     actual;         // 实际
        std::string     location;       // 定位：相对路径、条目身份或阶段名，可为空
        std::string     next_action;    // 下一步可执行动作
    };

    /**
     *  @brief 该类别是否为 warn 类别
     *  @param category 诊断类别
     *  @return spec §13.2 中无退出映射的类别返回 true
     *  @note warn 诊断不改变退出码，也不参与 select_exit_code 的选择
     */
    bool is_warning(ProjectCategory category) noexcept;

    /**
     *  诊断集合：追加顺序即发现顺序；输出前用 sorted() 取稳定顺序。
     *  纯数据载体只提供追加、查询与排序，不决定"该不该继续执行"（那是编排层的事）。
     */
    class DiagnosticSet
    {
    public:
        /**
         *  @brief 追加一条诊断，追加位置即发现顺序
         *  @param diagnostic 已填好五要素的诊断，按值移入
         */
        void add(ProjectDiagnostic diagnostic);

        /**
         *  @brief 集合是否为空
         */
        bool is_empty() const noexcept;

        /**
         *  @brief 是否存在错误级诊断（任一条非 warn）
         *  @return 有非 warn 诊断返回 true；只有 warn 诊断或空集合返回 false
         */
        bool has_error() const noexcept;

        /**
         *  @brief 是否含指定类别
         *  @param category 诊断类别
         */
        bool has_category(ProjectCategory category) const noexcept;

        /**
         *  @brief 诊断条数
         */
        std::size_t size() const noexcept;

        /**
         *  @brief 按下标取诊断（追加顺序）
         *  @param index 下标，从 0 开始
         *  @return 该位置的诊断；index 越界属调用方违约，返回一个静态空载体，不越界访问、不抛异常
         *  @note 空载体不承担业务语义（不是"没有诊断"的表达，那用 is_empty()）；调用方应先查 size()
         */
        const ProjectDiagnostic& at(std::size_t index) const noexcept;

        /**
         *  @brief 按 spec §13.1 稳定排序后的副本
         *  @return 新集合，不改本对象：先按退出码全序分组（warn 组置后），组内按 subject 的 UTF-8 字节序
         *  @note UTF-8 字节序等于码点序，因此不解码即可满足"类别内对象名码点升序"
         *  @note 稳定排序：subject 相同时保持发现顺序，同一输入重复运行输出一致
         */
        DiagnosticSet sorted() const;

        /**
         *  @brief 取连续只读视图，顺序即追加顺序
         *  @note 只读；视图在下一次 add() 或本集合析构后失效，调用方不得保存超出该生命周期的指针
         */
        std::span<const ProjectDiagnostic> span() const noexcept;

    private:
        std::vector<ProjectDiagnostic> m_diagnostics = {};      // 追加顺序即发现顺序
    };

    /**
     *  @brief 按全序取唯一退出码
     *  @param diagnostics 全部诊断；warn 类别不参与选择
     *  @param has_plan_diff 计划只有变更、没有冲突时为真，即 spec 的差异码 6
     *  @return 无诊断且无差异时返回 kExitSuccess；多个非 warn 类别同时存在时取 exit_priority 最小者
     *  @note 差异判定不属于本函数：check_diff 不是诊断类别，值由 U5 的 has_plan_diff 给出；
     *        计划里存在冲突时调用方必须传假（冲突类别自己给码）
     */
    int select_exit_code(std::span<const ProjectDiagnostic> diagnostics, bool has_plan_diff) noexcept;
} // namespace ID::Project
