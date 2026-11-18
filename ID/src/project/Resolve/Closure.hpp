#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "project/Base/Diagnostic.hpp"
#include "project/Input/PluginCatalog.hpp"
#include "project/Resolve/EffectiveSelection.hpp"

/**
 *  依赖闭包、稳定拓扑序、链接边与编译面事实。
 *
 *  闭包只沿 `dependencies` 扩张：`links_when_on` 只决定"已在闭包里的上游是否链接"，不扩充闭包。
 *  编译面事实（`source_dirs`、`include_roots`、`vendor_links`）在这里一次算清，生成侧不再读清单、
 *  也不按路径前缀推断 SYSTEM。
 *
 *  不变量：
 *  1. 直接项与补齐项同名时是同一节点，取直接项的 linkage 与选项，不产生第二份配置。
 *  2. 边集合的 `(from, to)` 唯一：上游同时出现在 `dependencies` 与某个生效选项的 `links_when_on` 时，
 *     只保留条件边，由 `controlling_options` 表达控制它的选项（多个选项按 OR 合并）。
 *  3. 拓扑序是构建期结果：被依赖者在前，同层按插件名 UTF-8 字节序；目录扫描顺序与书写顺序不参与。
 *  4. 本单元无任何写盘调用与锁操作。
 *
 *  离线工具、非运行时热路径：本模块不在逐帧或逐元素路径上，std::string / std::vector 的分配成本可接受，
 *  因此不做逐点分配失败检查——这是对 cpp-style 第 9 节「分配失败不得忽略」的显式例外
 *  （范围仅限容器与字符串分配），与 project/Base 的既有口径一致。
 */

namespace ID::Project
{
    /**
     *  一条链接边：下游插件对上游插件的链接。`is_conditional` 为真时由 `controlling_options` 控制。
     */
    struct LinkEdge
    {
        std::string              from                = {};     // 下游插件（发起链接的一方）
        std::string              to                  = {};     // 上游插件
        bool                     is_conditional      = false;  // 受选项控制
        std::vector<std::string> controlling_options = {};     // 控制选项名，按字节序；无条件时为空
    };

    /**
     *  一个 include 根：路径口径与 CMake 侧一致（相对项目根）。
     */
    struct IncludeRoot
    {
        std::string path      = {};       // 如 "IDPlugin/IDJson/include"
        bool        is_system = false;    // 落在某个 vendor_dirs 条目之下：SYSTEM PUBLIC
    };

    /**
     *  一条共享 vendor 链接：只记被本插件实际链接的目标。
     */
    struct VendorLink
    {
        std::string dir    = {};   // 共享 vendor 目录名，落 IDPlugin/Vendor/<dir>
        std::string target = {};   // 该目录提供的 CMake 目标名
    };

    /**
     *  一个插件的编译面事实：生成接线与 resolved 记录的唯一来源。
     */
    struct PluginCompileFacts
    {
        std::string               plugin        = {};
        std::vector<std::string>  source_dirs   = {};   // 相对项目根的收源目录，按字节序
        std::vector<IncludeRoot>  include_roots = {};   // 按 path 字节序
        std::vector<VendorLink>   vendor_links  = {};   // 按 (dir, target) 排序
    };

    /**
     *  一个被闭包补齐的插件：它不是直接选择，而是某个下游的 `dependencies` 引入的。
     *  用于按 spec §10.1 以 `info` 说明"补齐"这件事（不是警告，也不改写用户的选择文件）。
     */
    struct ClosedBackPlugin
    {
        std::string plugin      = {};   // 被补齐的插件名
        std::string required_by = {};   // 声明该依赖的下游插件名（多个时取字节序最小者）
    };

    /**
     *  一次解析的结果：闭包内的有效选择、拓扑序、边集合、保留集素材与编译面事实。
     */
    struct Resolution
    {
        EffectiveSelection              selection      = {};   // 展开后的有效选择（all 已补齐）
        std::vector<std::string>        init_order     = {};   // 拓扑序：被依赖者在前
        std::vector<LinkEdge>           edges          = {};   // 有向边全集，按 (from, to) 排序
        std::vector<std::string>        common_headers = {};   // Common 头保留集合（相对 IDPlugin/Common）
        std::vector<std::string>        vendor_dirs    = {};   // 需接入的 shared vendor 目录名，按字节序
        std::vector<PluginCompileFacts> compile_facts  = {};   // 按插件名排序
        std::vector<ClosedBackPlugin>   closed_back    = {};   // 闭包补齐项，按插件名字节序
        DiagnosticSet                   diagnostics    = {};
        bool                            is_valid       = false;
    };

    /**
     *  @brief 由有效选择与清单元数据求闭包、拓扑序、链接边与编译面事实
     *  @param selection 已展开的直接选择（is_valid 为假时直接返回同诊断的无效结果）
     *  @param catalog 插件目录（闭包内插件的清单来源）
     *  @return 解析结果；`selection.all` 在本函数内补齐
     *  @note 闭包只沿 dependencies 扩张；补齐项 linkage 由 requirements 推出、选项取清单 default
     *  @note 补齐项（闭包里有、直接选择里没有）同时记入 `closed_back`，附声明该依赖的下游插件名，
     *        供调用方按 spec §10.1 以 `info` 说明；本函数自身不写日志、不改用户选择
     *  @note 成环报 dependency_cycle（环路以插件名序列写出）；闭包命中被忽略/未发现的插件报
     *        plugin_unknown 并附原始忽略原因，不把缺失上游移出闭包后继续
     */
    Resolution resolve_closure(const EffectiveSelection& selection, const PluginCatalog& catalog);

    /**
     *  稳定拓扑排序的结果。
     */
    struct TopoResult
    {
        bool                     is_ok = false;
        std::vector<std::string> order = {};   // 被依赖者在前；同层按名字字节序
        std::vector<std::string> cycle = {};   // 成环时的环路，按遍历顺序
    };

    /**
     *  @brief 稳定拓扑排序
     *  @param nodes 节点名集合
     *  @param edges 有向边（`from` 依赖 `to`）
     *  @return 排序结果；成环时 is_ok 为假并给出环路
     *  @note 不使用目录扫描顺序、清单书写顺序或文件系统返回顺序；同层按名字字节序
     */
    TopoResult topological_order(std::span<const std::string> nodes, std::span<const LinkEdge> edges);
} // namespace ID::Project
