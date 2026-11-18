#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "project/Base/Diagnostic.hpp"
#include "project/Base/Digest.hpp"
#include "project/Base/Inject.hpp"

/**
 *  文件读写机制：读取、原子写入、幂等跳过、流式复制与回读校验、目录列举、文件摘要与时间。
 *
 *  本头只回答"写没写成、读到没有"，不回答"该不该写"：冲突与所有权判定在 U5，本头不做业务判断。
 *
 *  不变量：
 *  1. 原子替换不删原目标：先写同目录临时文件，成功后才替换；替换前不重读目标
 *     （重读与摘要复验由 U5 的 TOCTOU 复验负责，避免同一检查两处实现）。
 *  2. 临时文件名固定为 `<目标名>.idtmp.<pid>.<seq>`，只清理本进程创建的临时文件，不按通配符删除。
 *  3. 幂等跳过按内容比较，不按时间戳：内容相同即不重写，避免无意义的时间戳变化与构建失效。
 *  4. 不提供"递归删除目录"，也不提供任何按模式批量删除：整目录误删在接口层就不可能。
 *  5. 大文件流式处理，不整文件读入内存（memory.md 第 9 节）。
 *
 *  离线工具、非运行时热路径：本模块不在逐帧或逐元素路径上，std::string / std::vector 的分配成本可接受，
 *  因此不做逐点分配失败检查——这是对 cpp-style 第 9 节「分配失败不得忽略」的显式例外（范围仅限容器与
 *  字符串分配），与 cmake/Common 的既有口径一致；文件内容缓冲用 new (std::nothrow) 分配，失败通过
 *  返回值表达。
 */

namespace ID::Project
{
    /**
     *  一次读取的结果：不存在与读取失败分开，前者是正常状态（用于幂等跳过与三态判定）。
     */
    enum class ReadStatus : std::uint8_t
    {
        Found    = 0,   // 读到完整内容
        NotFound = 1,   // 目标路径上没有文件
        Failed   = 2,   // 打开或读取失败
    };

    /**
     *  读取结果载体：bytes 只在 status == Found 时有效。
     */
    struct FileRead
    {
        ReadStatus  status  = ReadStatus::NotFound;     // 读取状态
        std::string bytes   = {};                       // 文件内容（二进制安全）
    };

    /**
     *  @brief 读取整个文件
     *  @param path 文件路径
     *  @return 状态与内容；NotFound 与 Failed 分开表达，调用方不需要再看 errno
     *  @note 只读；不跟随链接做额外判断（链接跟随由调用方的重解析点检查负责）
     */
    FileRead read_file(const std::filesystem::path& path);

    /**
     *  @brief 只读探测路径是否存在
     *  @param path 路径
     *  @param is_ok 属性查询是否成功（权限不足等为假；"不存在"算查询成功）
     *  @return 存在返回 true；不存在或查询失败返回 false
     *  @note 不抛异常。四个探测函数统一把"不存在"从查询错误里剥出来：部分标准库实现会把状态查询的
     *        ENOENT 报成错误码，若直接透传，调用方无法把"文件不存在"与"权限不足"分开
     */
    bool path_exists(const std::filesystem::path& path, bool& is_ok) noexcept;

    /**
     *  @brief 只读探测是否是普通文件
     *  @param path 路径
     *  @param is_ok 属性查询是否成功（"不存在"算查询成功）
     *  @return 是普通文件返回 true；链接按链接本身判定，不跟随其目标
     */
    bool is_regular_file(const std::filesystem::path& path, bool& is_ok) noexcept;

    /**
     *  @brief 只读探测是否是目录
     *  @param path 路径
     *  @param is_ok 属性查询是否成功（"不存在"算查询成功）
     *  @return 是目录返回 true；指向目录的链接按链接本身判定，不跟随其目标
     */
    bool is_directory(const std::filesystem::path& path, bool& is_ok) noexcept;

    /**
     *  @brief 只读探测是否为空目录
     *  @param path 路径
     *  @param is_ok 属性查询是否成功（"不存在"算查询成功）
     *  @return 是空目录返回 true；不存在、是链接、非目录或非空都返回 false
     */
    bool is_empty_directory(const std::filesystem::path& path, bool& is_ok) noexcept;

    /**
     *  目录列举结果：只列一层，按名字的 UTF-8 字节序排序，保证确定性。
     */
    struct DirectoryList
    {
        bool                     is_ok = false;     // 列举是否成功
        std::vector<std::string> names = {};        // 仅名字，不含路径；不区分文件与目录
    };

    /**
     *  @brief 列举目录下一层条目名
     *  @param path 目录路径
     *  @return 排序后的条目名；目录不存在或不可读时 is_ok 为假
     *  @note 只读；不递归、不跟随链接
     */
    DirectoryList list_directory(const std::filesystem::path& path);

    /**
     *  原子写入的一次请求。纯数据载体，由调用方一次填好；成功后不修改本结构。
     */
    struct WriteRequest
    {
        std::filesystem::path   target                  = {};                       // 目标绝对路径
        std::string             bytes                   = {};                       // 待写入内容（二进制安全）
        bool                    is_skip_if_identical    = true;                     // 内容相同则幂等跳过（spec §8.7）
        bool                    is_flush_required       = true;                     // 替换前同步到磁盘，缩小断电窗口
        InjectPoint             point                   = InjectPoint::None;        // 崩溃/失败注入点（Inject.hpp）
        std::string             subject                 = {};                       // 诊断对象：相对路径
    };

    /**
     *  原子写入的结果：is_ok 表达"没发生 I/O 失败"，is_written 表达"是否真的替换了目标"。
     *  幂等跳过时 is_ok 为真、is_written 为假。
     */
    struct WriteOutcome
    {
        bool                                is_ok       = false;   // 未发生 I/O 失败
        bool                                is_written  = false;   // 是否真的替换了目标（幂等跳过为 false）
        std::optional<ProjectDiagnostic>    diagnostic  = {};      // 失败原因（write_failed）；成功为空
    };

    /**
     *  @brief 写同目录临时文件 → flush → 原子替换目标
     *  @param request 一次写入的完整请求
     *  @return is_ok 与 is_written 分别表达成败与是否真的写了；失败时 diagnostic 给出五要素
     *  @note 临时文件名 `<目标名>.idtmp.<pid>.<seq>`；父目录不存在时失败（调用方先按 plan 建目录，
     *        不在此隐式建目录）
     *  @note 只清理本进程创建的临时文件；替换失败时原目标保持不变
     */
    WriteOutcome write_file_atomic(const WriteRequest& request);

    /**
     *  删除单个文件的结果：不存在视为成功（is_ok 为真、is_removed 为假）。
     */
    struct RemoveOutcome
    {
        bool                                is_ok       = false;   // 未发生 I/O 失败
        bool                                is_removed  = false;   // 是否真的删除了目标
        std::optional<ProjectDiagnostic>    diagnostic  = {};      // 失败原因；成功为空
    };

    /**
     *  @brief 删除单个文件
     *  @param path 文件路径
     *  @return 目标不存在时 is_ok 为真、is_removed 为假；删除失败时 is_ok 为假并带诊断
     *  @note 目标为目录时按 write_failed 失败（本函数不递归、不按模式删）
     *  @note 类型与存在性用不跟随链接的状态判断：链接本身按可删除的文件处理，悬垂链接不会被漏删
     */
    RemoveOutcome remove_file(const std::filesystem::path& path);

    /**
     *  @brief 删除空目录
     *  @param path 目录路径
     *  @return 目录不存在时 is_ok 为真、is_removed 为假；非空目录按 write_failed 失败
     *  @note 不递归、不允许按通配符批量删：非空即失败（memory.md 第 7 节）
     *  @note 指向目录的链接按"不是目录"处理：删除阶段不跟随链接，避免删掉链接目标的内容
     */
    RemoveOutcome remove_empty_directory(const std::filesystem::path& path);

    /**
     *  @brief 创建目录（含必要父目录）
     *  @param path 目录路径
     *  @return 已存在视为成功（is_ok 为真、is_removed 恒为假）；创建失败时带诊断
     *  @note 只创建路径本身与缺失的父目录，不预先铺开其它骨架；is_removed 在本函数恒为假，
     *        复用 RemoveOutcome 是为了让编排层的失败处理只有一套分支
     */
    RemoveOutcome create_directories(const std::filesystem::path& path);

    /**
     *  流式复制的结果：digest 是源内容摘要，成功后有效（记录里据此写 manifest 条目）。
     */
    struct CopyOutcome
    {
        bool                                is_ok       = false;   // 未发生 I/O 失败
        bool                                is_written  = false;   // 是否真的替换了目标（幂等跳过为 false）
        Digest                              digest      = {};      // 源内容摘要，成功后有效
        std::optional<ProjectDiagnostic>    diagnostic  = {};      // 失败原因；成功为空
    };

    /**
     *  @brief 复制文件：流式读取 + 目标侧原子写，写完回读校验摘要
     *  @param source 源文件绝对路径
     *  @param request 目标侧请求；bytes 字段被忽略，由本函数流式填充
     *  @return is_written 与 diagnostic 语义同 write_file_atomic；回读摘要不符按 write_failed 失败
     *  @note 不移动、不删除源文件；不把整个文件读入内存（memory.md 第 9 节）
     *  @note 源文件缺失或不可读同样按 write_failed 返回：类别选择是调用方（U5）的业务判断，
     *        本函数只报"复制没成"
     */
    CopyOutcome copy_file_atomic(const std::filesystem::path& source, const WriteRequest& request);

    /**
     *  单个文件的摘要、大小与最后修改时间。
     *  mtime 只作信息字段（spec §12.5：不用修改时间判定构建成功）。
     */
    struct FileStat
    {
        bool            is_ok               = false;    // 查询与摘要是否都成功
        Digest          digest              = {};       // 文件内容摘要
        std::int64_t    mtime_unix_seconds  = 0;        // 最后修改时间（Unix 秒）
        std::uint64_t   size_bytes          = 0;        // 文件字节数
    };

    /**
     *  @brief 读取单个文件的摘要、大小与最后修改时间
     *  @param path 文件路径
     *  @return 查询失败或摘要失败时 is_ok 为假，其它字段未定义
     *  @note 只读；流式计算摘要，不整文件读入内存
     */
    FileStat stat_file(const std::filesystem::path& path);
} // namespace ID::Project
