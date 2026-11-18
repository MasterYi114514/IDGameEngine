#include "project/Base/Inject.hpp"

#include <cstdlib>

#include "project/Base/Text.hpp"

/**
 *  注入点的解析与触发实现。
 *
 *  标识串表与 `docs/id/11-verification-matrix.md` §11.3 的登记表逐项一致，也与 `docs/id/01`
 *  §1.9 的点位顺序一致：新增点必须先改那两张表，本文件只做登记，不私加分支。
 */

namespace ID::Project
{
    namespace
    {
        struct InjectName
        {
            std::string_view    identifier;     // 登记标识串，与 11-verification-matrix.md §11.3 逐字一致
            InjectPoint         point;          // 对应点位
        };

        // 登记表：顺序与 InjectPoint 的取值顺序一致，便于人工核对
        inline constexpr InjectName kInjectNames[] =
        {
            { "p2.before-intent",          InjectPoint::P2BeforeIntent },
            { "p2.after-registry",         InjectPoint::P2AfterRegistry },
            { "p3.before-first-copy",      InjectPoint::P3BeforeFirstCopy },
            { "p3.after-first-copy",       InjectPoint::P3AfterFirstCopy },
            { "p3.before-payload",         InjectPoint::P3BeforePayload },
            { "p4.before-fragment",        InjectPoint::P4BeforeFragmentWrite },
            { "p5.before-delete",          InjectPoint::P5BeforeFirstDelete },
            { "p5.after-delete",           InjectPoint::P5AfterFirstDelete },
            { "p6.after-manifest-temp",    InjectPoint::P6AfterManifestTemp },
            { "p6.after-manifest-rename",  InjectPoint::P6AfterManifestRename },
            { "p6.after-resolved-rename",  InjectPoint::P6AfterResolvedRename },
            { "p6.after-intent-delete",    InjectPoint::P6AfterIntentDelete },
            { "p6.before-marker",          InjectPoint::P6BeforeMarkerWrite },
            { "build.before-query",        InjectPoint::BuildBeforeQueryWrite },
            { "build.before-configure",    InjectPoint::BuildBeforeConfigure },
            { "build.after-configure",     InjectPoint::BuildAfterConfigure },
            { "build.before-build",        InjectPoint::BuildBeforeBuild },
            { "build.after-build",         InjectPoint::BuildAfterBuild },
            { "build.before-log",          InjectPoint::BuildBeforeLogWrite },
            { "build.before-record",       InjectPoint::BuildBeforeRecordWrite },
            { "build.after-record-temp",   InjectPoint::BuildAfterRecordTemp },
        };

        // TU 私有的可变状态：解析结果只有本翻译单元可达，配置只经 init_injection 写入
        InjectConfig g_inject_config = {};

        InjectPoint point_from_identifier(std::string_view text) noexcept
        {
            for (const InjectName& entry : kInjectNames)
            {
                if (entry.identifier == text)
                {
                    return entry.point;
                }
            }
            return InjectPoint::None;
        }

        InjectAction action_from_identifier(std::string_view text) noexcept
        {
            if (text == "fail")
            {
                return InjectAction::Fail;
            }
            if (text == "abort")
            {
                return InjectAction::Abort;
            }
            return InjectAction::None;
        }

        // 拆分 "名称=动作"：返回是否是合法分割（有且仅有一个 '='，两侧都非空）
        bool split_assignment(std::string_view text, std::string_view& name, std::string_view& value) noexcept
        {
            const std::size_t position = text.find('=');
            if (position == std::string_view::npos)
            {
                return false;
            }
            name  = trim_ascii_space(text.substr(0, position));
            value = trim_ascii_space(text.substr(position + 1));
            return !name.empty() && !value.empty();
        }
    } // namespace

    bool init_injection(std::string_view text)
    {
        g_inject_config = InjectConfig{};

        std::size_t begin = 0;
        for (;;)
        {
            const std::size_t      separator = text.find(';', begin);
            const std::size_t      end       = separator == std::string_view::npos ? text.size() : separator;
            const std::string_view item      = trim_ascii_space(text.substr(begin, end - begin));

            std::string_view name  = {};
            std::string_view value = {};
            if (!item.empty() && split_assignment(item, name, value))
            {
                const InjectPoint  point  = point_from_identifier(name);
                const InjectAction action = action_from_identifier(value);
                if (point != InjectPoint::None && action != InjectAction::None)
                {
                    // 只保留第一个可识别的点：多点同时武装会让故障归因不确定
                    g_inject_config.point    = point;
                    g_inject_config.action   = action;
                    g_inject_config.is_armed = true;
                    return true;
                }
            }

            if (separator == std::string_view::npos)
            {
                return false;
            }
            begin = separator + 1;
        }
    }

    const InjectConfig& inject_config() noexcept
    {
        return g_inject_config;
    }

    bool hit_inject_point(InjectPoint point)
    {
        // 默认路径（未设置环境变量）只到这里：一次已缓存的布尔读 + 一次分支
        if (!g_inject_config.is_armed)
        {
            return false;
        }
        if (g_inject_config.point != point)
        {
            return false;
        }

        // 只触发一次：触发后解除武装，重跑同一命令不会再次注入
        g_inject_config.is_armed = false;

        if (g_inject_config.action == InjectAction::Abort)
        {
            // 不跑析构、不删锁：正是"进程被杀"的语义
            std::_Exit(kInjectExitCode);
        }
        return g_inject_config.action == InjectAction::Fail;
    }

    std::string_view to_identifier(InjectPoint point) noexcept
    {
        for (const InjectName& entry : kInjectNames)
        {
            if (entry.point == point)
            {
                return entry.identifier;
            }
        }
        return "unknown";
    }
} // namespace ID::Project
