#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "cmake/Common/Diagnostic.hpp"
#include "cmake/Directory/Directory.hpp"
#include "cmake/Value/Argument.hpp"

/**
 *  工程层：持有根目录与外部目标登记，提供深度优先遍历顺序、待落盘清单与生成前校验入口。
 *
 *  职责边界：只持有与遍历，不做排版、不产文本、不落盘、不读文件系统。单个目标与目录的排版归
 *  emit_target / emit_directory，待落盘清单归 collect_outputs，结构校验归 validate。
 *  工程名只取自根目录名（get_name 转发 root().get_name()），不存在第二份可能与之不一致的名字。
 *
 *  所有权：根目录由 std::unique_ptr 持有，子目录由各自的父目录持有，每个对象只有一条拥有路径。
 *
 *  离线工具、非运行时热路径：本模块不在逐帧或逐元素路径上，std::string / std::vector 的分配成本可接受，
 *  模型层因此不做逐点分配失败检查——这是对 cpp-style 第 9 节「分配失败不得忽略」的显式例外
 *  （依据：CMake 封装计划书第 1.2 节第 10 项，范围仅限容器与字符串分配）。
 */

namespace ID::CMake
{
    /**
     *  产出的文本范围。写入方式映射写死：WholeFile 只允许整文件写入，BodyOnly 只允许受控片段替换。
     *
     *  HeaderOnly + ConfigurationOnly + TargetsOnly 依次拼接（段与段之间一个空行）就是 WholeFile 的正文：
     *  根 CMakeLists.txt 的两个受控片段由同一套骨架顺序产出，不出现第二份顺序实现。
     *  三个新取值与 BodyOnly 一样不带 GeneratedHeader（受控片段内不写注释头）。
     */
    enum class EmitScope : std::uint8_t
    {
        WholeFile         = 0,   // 第 1、2 项 + 目录体（第 3–13 项）
        BodyOnly          = 1,   // 只有目录体，不含 cmake_minimum_required 与 project
        HeaderOnly        = 2,   // 第 1、2 项
        ConfigurationOnly = 3,   // 第 3、4 项：option、配置变量、分支块与配置块命令
        TargetsOnly       = 4,   // 第 5–13 项：收源、目标块与尾部命令
    };

    static_assert(sizeof(EmitScope) == sizeof(std::uint8_t),
                  "EmitScope 的底层类型必须与声明一致，否则写入方式映射的取值约定失效");

    /**
     *  待落盘文件：相对项目根的 POSIX 分隔路径与产出模式。数据载体不带判定。
     */
    struct OutputFile
    {
        std::string relative_path;                    // 相对项目根，如 "IDPlugin/IDJson/CMakeLists.txt"
        EmitScope   scope = EmitScope::WholeFile;     // 模型一律给整个文件
    };

    /**
     *  一个工程：根目录 + 允许被链接但未建模的外部目标名。
     */
    class Project
    {
    public:
        /**
         *  @brief 创建工程的根目录
         *  @param name 工程名，同时是根目录名；只登记，空串由 validate 报 EmptyName
         */
        explicit Project(std::string_view name);

        Project(const Project&)            = delete;
        Project& operator=(const Project&) = delete;

        /**
         *  @brief 工程名：转发 root().get_name()
         */
        std::string_view get_name() const noexcept;

        /**
         *  @brief 根目录
         */
        Directory& root() noexcept;

        /**
         *  @brief 根目录（只读）
         */
        const Directory& root() const noexcept;

        /**
         *  @brief 目录总数（含根），按深度优先顺序计
         */
        std::size_t directory_count() const noexcept;

        /**
         *  @brief 按深度优先顺序取目录：父目录先于子目录，只对 create_subdirectory 建立的对象计数
         *  @param index 下标，必须小于 directory_count
         *  @note create_referenced_subdirectory 登记的条目没有目录对象，不在遍历序列里
         */
        const Directory& directory_at(std::size_t index) const noexcept;

        /**
         *  @brief 登记一个本工程未建模、但允许被链接的目标名；按名称去重，重复登记不产生诊断
         *  @param name 目标名，取值文本即比较键
         */
        void add_external_target(Argument name);

        /**
         *  @brief 某个名字是否已登记为外部目标
         */
        bool has_external_target(std::string_view name) const noexcept;

    private:
        std::unique_ptr<Directory> m_root             = {};   // 根目录，唯一拥有者
        std::vector<std::string>   m_external_targets = {};   // 按名称去重的外部目标登记集合
    };

    /**
     *  @brief 给出待落盘清单：根目录对应 CMakeLists.txt，其余目录对应 <相对路径>/CMakeLists.txt
     *  @param project 工程
     *  @return 按深度优先顺序排列的清单；模型一律给 EmitScope::WholeFile
     *  @note 不写文件、不读文件系统、不做路径合法性判断；哪些路径必须改成 BodyOnly 由组装阶段决定
     */
    std::vector<OutputFile> collect_outputs(const Project& project);

    /**
     *  @brief 生成前校验：收 const Project& 的唯一校验入口
     *  @param project 工程
     *  @return 校验报告；失败时 is_valid 为 false 并逐条带类别、对象、期望、实际、定位与下一步动作
     *  @note 在产出文本与落盘之前执行，失败时不产出文本、不写任何文件；
     *        emit_* 与 collect_outputs 只做排版与清单，不重复校验
     */
    ValidationReport validate(const Project& project);
} // namespace ID::CMake
