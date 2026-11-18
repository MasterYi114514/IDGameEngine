#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

#include "project/Base/Diagnostic.hpp"

/**
 *  项目锁与登记锁：独占创建、陈旧回收、持有者信息与进程存活判定。
 *
 *  不变量：
 *  1. 同一目标同一时刻只允许一个写入者：检出活跃写入者时报 writer_conflict，不排队、不抢占。
 *  2. 陈旧判定只看前三项（进程号、进程启动时间、主机名）：同主机且进程不存在或启动时间不符
 *     才回收；无法证明陈旧时按活跃处理，宁可失败也不误删他人锁。
 *  3. 陈旧回收顺序固定：先把锁文件改名为 `<锁名>.stale.<pid>.<seq>`，再独占创建新锁文件，
 *     最后删除改名后的旧文件；改名失败按 IoFailure 返回，不删除任何文件。
 *  4. 析构释放是兜底，正常路径在命令结束时显式 release()；任何退出路径都不得留下锁文件。
 *  5. 取锁顺序（项目锁 → 登记锁）由编排层保证，本头不提供"取登记锁时自动取项目锁"的便利函数。
 *
 *  离线工具、非运行时热路径：本模块不在逐帧或逐元素路径上，std::string / std::vector 的分配成本可接受，
 *  因此不做逐点分配失败检查——这是对 cpp-style 第 9 节「分配失败不得忽略」的显式例外（范围仅限容器与
 *  字符串分配），与 cmake/Common 的既有口径一致。
 */

namespace ID::Project
{
    /**
     *  锁的作用范围：只影响诊断文案，不改变锁协议。
     */
    enum class LockScope : std::uint8_t
    {
        Project  = 0,   // <项目根>/IDGenerated/.lock
        Registry = 1,   // <工程根>/ID/project.lock
    };

    /**
     *  取锁结果。IoFailure 与 WriterConflict 分开：前者是 I/O 问题，后者是并发语义。
     */
    enum class LockVerdict : std::uint8_t
    {
        Acquired        = 0,    // 已取得锁
        WriterConflict  = 1,    // 有活跃写入者：不排队、不抢占
        IoFailure       = 2,    // 无法创建、读取或改名锁文件
    };

    /**
     *  锁文件内容（UTF-8 文本，每行 `key=value`，键序固定）：pid / process_start / host / taken_at。
     *  纯数据载体：由 Lock.cpp 序列化写出，陈旧判定只读前三项，taken_at 只作人读信息。
     */
    struct LockOwner
    {
        std::uint32_t   pid             = 0;        // 进程号
        std::uint64_t   process_start   = 0;        // 进程启动时间原值（Windows: FILETIME；POSIX: /proc stat 第 22 列）
        std::string     host            = {};       // 主机名
        std::string     taken_at        = {};       // 取锁时间，ISO-8601 带 UTC 偏移
    };

    /**
     *  独占锁对象：构造即空，acquire 成功后才持有；析构即释放。
     *  只允许移动：复制会产生两份"持有同一锁文件"的对象。
     */
    class ProjectLock
    {
    public:
        ProjectLock() = default;
        ~ProjectLock();

        ProjectLock(const ProjectLock&)            = delete;
        ProjectLock& operator=(const ProjectLock&) = delete;
        ProjectLock(ProjectLock&& other) noexcept;
        ProjectLock& operator=(ProjectLock&& other) noexcept;

        /**
         *  取锁结果载体：verdict 表达结论，diagnostic 只在非 Acquired 或陈旧回收时非空。
         */
        struct AcquireResult
        {
            LockVerdict                         verdict              = LockVerdict::IoFailure;   // 取锁结论
            std::optional<ProjectDiagnostic>    diagnostic           = {};                      // 失败原因或回收说明
            bool                                is_stale_recovered   = false;                   // 是否回收了陈旧锁
        };

        /**
         *  @brief 独占创建并持有锁文件
         *  @param path 锁文件绝对路径；父目录必须已存在（目录创建是编排层的职责）
         *  @param scope 语义范围，只影响诊断文案
         *  @return 结论与诊断；WriterConflict 时诊断带持有者信息（进程号、主机、取锁时间）
         *  @note 检出已有锁时按 spec §2.5 判定陈旧：同主机且进程不存在或启动时间不符即回收，
         *        并以 warn 日志报告（不新增诊断类别，warn 级普通信息）；其它情况返回 WriterConflict
         *  @note 锁文件内容无法解析时按 WriterConflict 处理：不能证明陈旧就不删除他人文件
         *  @note 已持有同一路径时不重复创建，返回 Acquired；已持有其它路径返回 IoFailure
         */
        AcquireResult acquire(const std::filesystem::path& path, LockScope scope);

        /**
         *  @brief 释放锁：删除锁文件并清空持有状态
         *  @note 重复调用无副作用；删除失败不抛异常也不改变"已不再持有"的状态，避免析构路径失败扩大影响
         */
        void release() noexcept;

        /**
         *  @brief 当前是否持有锁
         */
        bool is_held() const noexcept;

        /**
         *  @brief 当前持有的锁文件路径；未持有时为空路径
         */
        const std::filesystem::path& path() const noexcept;

    private:
        std::filesystem::path   m_path      = {};       // 持有的锁文件路径；未持有时为空
        bool                    m_is_held   = false;    // 是否持有
    };

    /**
     *  @brief 当前进程号
     */
    std::uint32_t current_pid() noexcept;

    /**
     *  @brief 当前进程的启动时间原值
     *  @return Windows 为创建时间 FILETIME 的 64 位原值，POSIX 为 /proc/self/stat 第 22 列；
     *          取不到时返回 0（陈旧判定据此按"无法证明陈旧"处理）
     */
    std::uint64_t current_process_start() noexcept;

    /**
     *  @brief 当前主机名
     *  @return 主机名的 UTF-8 文本；取不到时返回空串
     */
    std::string current_host_name();

    /**
     *  @brief 判断进程是否存活且启动时间一致
     *  @param pid 进程号
     *  @param process_start 期望的启动时间原值
     *  @return 进程存在且启动时间与期望一致时返回 true；进程不存在或启动时间不符（进程号被复用）返回 false
     *  @note 无法查询时按"存活"返回 true：不误删他人的锁优先于回收效率
     */
    bool is_process_alive(std::uint32_t pid, std::uint64_t process_start) noexcept;

    /**
     *  @brief 当前本地时间的 ISO-8601 文本，带 UTC 偏移
     *  @return 形如 `2026-05-01T12:34:56+08:00`；锁文件与 build.json 共用
     *  @note 本地时间带偏移，不做时区换算以外的格式化；日志文件名另用 UTC 紧凑格式（由构建日志单元负责）
     */
    std::string iso8601_now_with_offset();
} // namespace ID::Project
