#pragma once

#include <cstdint>
#include <string_view>

/**
 *  测试注入点：只为 spec §15 的故障注入与崩溃注入用例而存在，不承载任何生产语义。
 *
 *  不变量：
 *  1. 未设置 `ID_TEST_INJECT` 时行为与没有本模块完全一致：每个注入点只做一次已缓存的布尔判断，
 *     不分配、不写盘、不改变返回值。
 *  2. 注入点表是公开登记（`docs/id/11-verification-matrix.md` §11.3）：点位名、标识串与动作必须与
 *     那张表逐项一致，实现不得新增未登记的点。
 *  3. `Abort` 用 `std::_Exit`：不跑析构、不删锁，正是"进程被杀"的语义；进程内测试不得对自身使用
 *     `Abort`，必须用子进程。
 *  4. `Fail` 用返回值表达，调用方按注入点登记的类别走既有诊断通道，不新增类别。
 *
 *  离线工具、非运行时热路径：本模块不在逐帧或逐元素路径上，解析期的一次分配不属于热路径成本，
 *  因此不做逐点分配失败检查——这是对 cpp-style 第 9 节「分配失败不得忽略」的显式例外（范围仅限
 *  容器与字符串分配），与 cmake/Common 的既有口径一致。
 */

namespace ID::Project
{
    /**
     *  注入点位。取值顺序即登记表顺序，Count 是哨兵（不是合法注入点）。
     */
    enum class InjectPoint : std::uint8_t
    {
        None                    = 0,    // 未命中任何点
        P2BeforeIntent          = 1,    // new：写 intent 之前
        P2AfterRegistry         = 2,    // 登记已写、plugins.json 未写
        P3BeforeFirstCopy       = 3,    // 首个复制之前
        P3AfterFirstCopy        = 4,    // 首个复制之后
        P3BeforePayload         = 5,    // before-payload 写入之前
        P4BeforeFragmentWrite   = 6,    // 片段写入之前
        P5BeforeFirstDelete     = 7,    // 首个删除之前
        P5AfterFirstDelete      = 8,    // 首个删除之后
        P6AfterManifestTemp     = 9,    // manifest 临时文件已写、未替换
        P6AfterManifestRename   = 10,   // 提交未完成窗口（manifest 已替换、resolved 未替换）
        P6AfterResolvedRename   = 11,   // 已提交、intent 未删
        P6AfterIntentDelete     = 12,   // intent 已删、标记未写
        P6BeforeMarkerWrite     = 13,   // 写 is_initialized 之前
        BuildBeforeQueryWrite   = 14,   // 写 File API 查询之前
        BuildBeforeConfigure    = 15,   // configure 之前
        BuildAfterConfigure     = 16,   // configure 已返回、build 未启动
        BuildBeforeBuild        = 17,   // build 步骤之前
        BuildAfterBuild         = 18,   // build 已返回、日志与记录未写
        BuildBeforeLogWrite     = 19,   // 打开或写日志之前
        BuildBeforeRecordWrite  = 20,   // 读旧 build.json 之前
        BuildAfterRecordTemp    = 21,   // build.json 已写临时文件、尚未原子替换
        Count                   = 22,   // 哨兵：不是合法注入点
    };

    /**
     *  注入动作。Fail 让调用点走既有诊断通道，Abort 立即终止进程。
     */
    enum class InjectAction : std::uint8_t
    {
        None  = 0,      // 未配置动作
        Fail  = 1,      // 该点返回 WriteFailed 或该点登记的类别，不中断进程
        Abort = 2,      // 立即 _Exit(kInjectExitCode)，模拟进程被杀死（只用于子进程测试）
    };

    /**
     *  注入配置：只描述一个点，is_armed 为假表示不再触发。
     *  纯数据载体，由 init_injection 一次写入，其余单元只读。
     */
    struct InjectConfig
    {
        InjectPoint     point       = InjectPoint::None;    // 已配置的点位
        InjectAction    action      = InjectAction::None;   // 该点的动作
        bool            is_armed    = false;                // 只触发一次，触发后置假
    };

    // 注入中止的退出码：与任何真实类别产生的退出码都不同，断言"等于 124"
    inline constexpr int kInjectExitCode = 124;

    /**
     *  @brief 解析 `ID_TEST_INJECT` 的文本并缓存
     *  @param text 形如 `p6.after-manifest-rename=abort` 或 `p3.before-payload=fail`；
     *              多个点用 ';' 分隔，本实现只保留第一个可识别的点
     *  @return 至少识别出一个注入点时返回 true；无法识别的文本由 CLI 层按用法错误报警（不影响退出码）
     *  @note 只在进程入口调用一次（U10）；未设置环境变量时不产生任何写操作
     *  @note 每次调用都从空配置重新解析，重复调用不会叠加
     */
    bool init_injection(std::string_view text);

    /**
     *  @brief 取当前注入配置
     *  @return 只读配置；未初始化时所有字段为默认值（is_armed 为假）
     *  @note 只读；不交出可写引用
     */
    const InjectConfig& inject_config() noexcept;

    /**
     *  @brief 到达一个注入点：已武装且点位匹配时按动作生效
     *  @param point 当前到达的点位
     *  @return true 表示调用方应把本次动作按失败处理（动作是 Fail）；其它情况返回 false
     *  @note Abort 不返回；Fail 只触发一次（触发后配置被解除武装）
     *  @note 返回 false 时语义与没有本函数完全一致：生产路径不得依赖注入点存在
     */
    bool hit_inject_point(InjectPoint point);

    /**
     *  @brief 取注入点的登记标识串
     *  @param point 注入点位
     *  @return 与登记表逐字一致的标识串，如 "p6.after-manifest-rename"、`build.after-record-temp`；
     *          未知取值返回 "unknown"
     *  @note 返回值指向静态存储，进程生命周期有效
     */
    std::string_view to_identifier(InjectPoint point) noexcept;
} // namespace ID::Project
