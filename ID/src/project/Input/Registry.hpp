#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "project/Base/Diagnostic.hpp"

/**
 *  `project.json`：本机项目登记（spec §3）。
 *
 *  只回答"登记文件里写了什么、能否作为登记使用"，并支持"只合并本条记录"的写回与确定性序列化。
 *  不判断项目名/路径的语义合法性（那是 CLI 与 P0 的职责），也不加锁——登记锁由编排层在持有项目锁
 *  期间短时取得，本单元拿到调用方的顺序保证后只做读改写。
 *
 *  不变量：
 *  1. 读取是只读的：文件不存在按空表处理（is_present 为假、is_valid 为真），不创建文件。
 *  2. 重复 name 或重复 path 一律 registry_invalid 并列出全部冲突记录，不按"取第一条"择一。
 *  3. 写回只替换同 name 同 path 的那一条，不整表重排、不改其它项目记录的字段。
 *  4. 序列化键序固定 name/path/is_initialized、4 空格缩进、以 '\n' 结尾；同一结构两次逐字节相同。
 *
 *  离线工具、非运行时热路径：本模块不在逐帧或逐元素路径上，std::string / std::vector 的分配成本可接受，
 *  因此不做逐点分配失败检查——这是对 cpp-style 第 9 节「分配失败不得忽略」的显式例外
 *  （范围仅限容器与字符串分配），与 project/Base 的既有口径一致。
 */

namespace ID::Project
{
    /**
     *  一条登记。`path` 保存规范化后的绝对路径 UTF-8 文本（正斜杠），规范化由调用方在传入前完成。
     */
    struct RegistryEntry
    {
        std::string name            = {};       // 项目名：CMake 工程名与可执行目标名
        std::string path            = {};       // 规范化绝对路径
        bool        is_initialized  = false;    // 是否已有一次成功提交的组装（派生缓存）
    };

    /**
     *  一次读取的结果。is_present 为假表示文件不存在（空表，is_valid 为真）。
     */
    struct RegistryFile
    {
        bool                        is_present  = false;    // 文件是否存在
        bool                        is_valid    = false;    // 能否作为登记使用
        std::vector<RegistryEntry>  entries     = {};       // 文件顺序，无语义
        DiagnosticSet               diagnostics = {};       // 非法时的类别与对象
    };

    /**
     *  @brief 读取 `<工程根>/ID/project.json`
     *  @param path 登记文件绝对路径
     *  @return 读取结果；文件不存在时 is_present 为假、is_valid 为真、entries 为空
     *  @note 违规映射：解析失败 / 顶层非数组 / 缺字段 / 类型不符 / 元素非对象 / 空名空路径 /
     *        重复 name 或重复 path（Windows 按 ASCII 折叠比较）→ registry_invalid，并列出全部冲突记录
     *  @note 只读文件，不创建、不修改
     */
    RegistryFile load_registry(const std::filesystem::path& path);

    /**
     *  @brief 按项目名查找登记
     *  @return 命中时返回条目指针，否则返回 nullptr
     *  @note 比较按项目名字节相等；指针在 registry 生命周期内有效
     */
    const RegistryEntry* find_by_name(const RegistryFile& registry, std::string_view name) noexcept;

    /**
     *  @brief 按规范化路径查找登记
     *  @param normalized_path 已规范化的绝对路径文本
     *  @return 命中时返回条目指针，否则返回 nullptr
     *  @note 比较只有一个入口 `is_same_absolute_path`：平台路径规则 + Windows 8.3 短名展开，因此同一目录的
     *        短名/别名不会被当成两条登记（spec §2.1）
     */
    const RegistryEntry* find_by_path(const RegistryFile& registry, std::string_view normalized_path);

    /**
     *  写回结果：is_ok 表达成败，失败时 diagnostic 给出五要素。
     */
    struct RegistryWriteOutcome
    {
        bool                             is_ok      = false;
        std::optional<ProjectDiagnostic> diagnostic = {};
    };

    /**
     *  @brief 只合并本条登记后原子写回
     *  @param registry_path 登记文件绝对路径
     *  @param entry 本条记录（name/path/is_initialized 的新取值）
     *  @param is_new_entry 为真表示这是新登记：同 name 或同 path 已被其它记录占用时报 project_exists
     *  @return 成功 / 失败；失败按 registry_invalid（读失败）或 write_failed（写失败）
     *  @note 必须在持有登记锁期间调用（spec §2.5）；实现是"读当前文件 → 只替换同 name 同 path 的那一条
     *        → 原子替换"，不整表重排、不改其它项目记录的字段
     */
    RegistryWriteOutcome write_registry_entry(const std::filesystem::path& registry_path,
                                              const RegistryEntry& entry,
                                              bool is_new_entry);

    /**
     *  @brief 序列化登记文件（供自检与测试构造；正常写回走 write_registry_entry）
     *  @param entries 登记条目，按传入顺序写出
     *  @return 4 空格缩进、键序固定为 name/path/is_initialized、以 '\n' 结尾的文本
     */
    std::string serialize_registry(const std::vector<RegistryEntry>& entries);
} // namespace ID::Project
