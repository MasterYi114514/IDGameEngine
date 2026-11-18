#pragma once

#include <filesystem>
#include <optional>

#include "project/Base/Digest.hpp"
#include "project/Base/FileIo.hpp"

/**
 *  只读的磁盘摘要探测：把"目标不存在""目标存在但不是普通文件""读取失败"三种情形分开，
 *  供计划与执行阶段在 D 侧取摘要（`(path, fragment)` 的整文件粒度）。
 *
 *  不变量：
 *  1. 只读：不创建、不删除、不修改任何文件，也不建目录。
 *  2. `digest` 为空表示目标不存在（D = absent）；`is_ok` 为假表示属性查询或读取失败，
 *     调用方按各自的失败类别处理，不把"读不到"当成"不存在"。
 *  3. 存在但不是普通文件时 `is_ok` 为假：类型不符按 spec §2.3 拒绝，不做类型替换。
 *  4. 摘要流式计算，不整文件读入内存（`references/memory.md` §9）。
 */

namespace ID::Project
{
    /**
     *  一次探测的结果：`is_ok` 为假表示查询或读取失败；`digest` 为空表示目标不存在。
     */
    struct PathDigest
    {
        bool                  is_ok  = true;    // 属性查询与摘要读取是否都成功
        std::optional<Digest> digest = {};      // 目标存在时的整文件摘要；不存在为空
    };

    /**
     *  @brief 探测一个文件路径的整文件摘要
     *  @param absolute 目标绝对路径
     *  @return 探测结果；不存在时 `digest` 为空、`is_ok` 为真
     *  @note 只读；不跟随链接改写目标，也不做大小写归一
     */
    inline PathDigest probe_file_digest(const std::filesystem::path& absolute)
    {
        PathDigest result;

        bool is_query_ok = false;
        const bool exists = path_exists(absolute, is_query_ok);
        if (!is_query_ok)
        {
            result.is_ok = false;
            return result;
        }
        if (!exists)
        {
            return result;
        }

        bool is_type_ok = false;
        const bool is_file = is_regular_file(absolute, is_type_ok);
        if (!is_type_ok || !is_file)
        {
            result.is_ok = false;
            return result;
        }

        bool is_read_ok = false;
        const Digest digest = digest_file(absolute, is_read_ok);
        if (!is_read_ok)
        {
            result.is_ok = false;
            return result;
        }

        result.digest = digest;
        return result;
    }
} // namespace ID::Project
