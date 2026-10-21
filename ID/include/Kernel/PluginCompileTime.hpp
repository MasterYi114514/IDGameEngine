#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

#include "Kernel/CoreTypes.hpp"

/**
 *  编译期插件声明与静态产品组装（ID::PluginCompileTime）
 *  1. 本头只承载“编译期”数据与算法：拥有存储的插件身份、不等长二维候选清单、
 *     能力/槽位/接口修订契约、构建证据，以及把完整 catalog + 显式选中集合解析成
 *     唯一绑定、去重边、确定性拓扑序与指纹的纯常量求值实现；
 *  2. 对外操作只有 `ID::PluginManager` 上的三个静态 `consteval` 操作族
 *     （register_plugin / declare_dependencies / sort_and_validate）。本头里的
 *     `ID::PluginCompileTime::detail` 全部是内部 `constexpr` 实现，不构成第四个公共入口；
 *  3. 所有返回数据自带拥有存储：PluginIdentity 复制名字字节，PluginDeclaration 把候选名写进
 *     自己的字节池，AssemblyResult 只存身份、索引、槽位归属与诊断，不保存指向常量求值局部
 *     对象或自身成员的指针，因此复制/移动不会产生悬空 view；
 *  4. 运行期零成本：全部结论在常量求值期算完，产物是编译期常量；本头不引入锁、分配、
 *     字符串查找或任何运行期分支；
 *  5. 诊断分阶段（AssemblyPhase），阶段优先级固定：
 *     声明/身份/契约 -> catalog 候选引用 -> 选中与唯一绑定 -> 能力/槽位 -> 固定图 -> 构建证据
 *     -> 运行期绑定元数据。低阶段错误不会因为高阶段的提前中断而被掩盖；
 *     `Shape::kActiveCapacity > 128` 属于 Selection，必须先完成 Declaration/CatalogBinding 检查才报。
 *  5.1 声明组内顺序固定：组区间/候选上限/修订容量与候选格式、自依赖、组内重复候选先判，
 *     之后才是"逐候选修订记录与候选名集合一一对应"。因此同一组出现两条同名候选得到
 *     DuplicateCandidate，而修订记录自己重复占用同一个候选仍是 ContractCandidateMismatch。
 *  5.2 名字定位只走固定编译期索引，不做重复线性扫描：catalog 名 -> catalog 下标是开放寻址表
 *     （容量 2 倍、探测有界、命中同 ID 后必须满名确认），catalog 下标 -> canonical active 下标是
 *     定长反查数组，active 名 -> 下标是 canonical 有序数组上的二分；构建证据与绑定阶段因此
 *     不必对每条记录做 O(active) 名字扫描。
 *  6. provides 容量不是全局固定 4：PluginCatalogEntry<G, C, P> 的 P 由调用方类型给出，
 *     Shape 从全部条目的 P 求和推导 result 的扁平 provision 容量；每个 provision 独立记录
 *     能力名、接口修订、槽位与 canonical active owner，不再用单个 active_slot/active_api_revision
 *     代表整个插件。槽位数量仍是固定的 64，能力个数不受 4 限制。
 *
 *  @note 唯一性判定绝不使用 64 位哈希或 16 字节前缀当“相等”：先比 (长度, ID, 前缀字) 加速，
 *        命中后再逐字节确认完整名字，因此“不同名但同哈希”会被单独诊断为 NameHashCollision，
 *        不会被误判成同名插件或自依赖。
 */

namespace ID::PluginCompileTime
{
    // ---- 容量预算（计划书 3.1 / 6.5 的首版取值，属容量上界而非 ABI 自然定律） ----
    inline constexpr size_t   kMaxNameBytes    = 96;      // 插件/能力名最大字节数（不含末尾 NUL）
    inline constexpr size_t   kNameCapacity    = kMaxNameBytes + 1;
    inline constexpr size_t   kMaxCatalog      = 256;     // catalog 上限（独立于 active 上限）
    inline constexpr size_t   kMaxActive       = 128;     // 本产品选中插件上限
    inline constexpr size_t   kMaxRequirements = 32;      // 每插件需求组上限（外层 AND 的组数）
    inline constexpr size_t   kMaxCandidates   = 16;      // 每个需求组的候选上限（内层 OR 的叶子数）
    inline constexpr size_t   kMaxSlots        = 64;      // 服务槽位数量（与 ServiceSlot::MaxSlots 同值），不是能力个数上限
    inline constexpr size_t   kMaxTargetBytes  = 96;      // CMake 目标名最大字节数
    inline constexpr size_t   kMaxNamespaceBytes = 96;    // C++ 命名空间最大字节数
    inline constexpr size_t   kMacroCapacity   = 128;     // ID_USE_PLUGIN_<TARGET_ID> 缓冲上限
    inline constexpr uint16_t kNoIndex         = 0xFFFF;  // 无下标哨兵（索引前必须先判等）
    inline constexpr int16_t  kNoSlot          = -1;      // 无服务槽位哨兵；0..63 是合法槽位
    inline constexpr uint32_t kSchemaRevision  = 1;       // 组装输入的结构修订，进入指纹

    /**
     *  @brief 组装期结构化错误类别（计划书第 7 节每条校验项对应一个稳定类别）
     *  @note 反向编译用例按枚举值断言类别，因此枚举值属于公共契约：增加类别或改名必须同步更新用例；
     *        类别按阶段分组排列，组内顺序不代表报告优先级，优先级由 AssemblyPhase 决定。
     */
    enum class PluginError : uint8_t
    {
        None = 0,                           // 无错误，计划可执行

        // ---- 阶段 1：声明 / 身份 / 契约（AssemblyPhase::Declaration） ----
        InvalidName,                        // 名字为空/超长/首字符非字母/含非法字符/内部 NUL
        InvalidTargetName,                  // CMake 目标名为空/超长/字符非法
        InvalidNamespace,                   // C++ 命名空间为空/超长/段名非法
        InvalidDeclaration,                 // 声明内部不变量或容量被写坏
        SchemaMismatch,                     // 输入的结构修订与当前头文件的 kSchemaRevision 不一致
        TooManyCatalog,                     // catalog 条目超过 256
        TooManyRequirements,                // 某插件需求组数超过 32
        TooManyCandidates,                  // 某需求组候选数超过 16
        TooManyProvides,                    // 某插件 provides 条目超过该 catalog 条目类型声明的容量（不是全局固定上限）
        EmptyRequirementGroup,              // 空内层：[[]] 不是合法需求
        DuplicateCandidate,                 // 同一需求组内同名候选（先于契约逐候选匹配判定，满名相等才算重复）
        SelfDependency,                     // 候选包含所属插件自身
        InvalidCapability,                  // 能力名为空/超长/字符非法
        InvalidRevision,                    // 接口修订为 0（接口修订不能省，也不能用插件发布版本冒充）
        InvalidRevisionRange,               // 修订区间非法：min == 0 或 min > max
        InvalidSlot,                        // 槽位越界（> 63）；一个插件可以声明多个不同槽位
        InterfaceLifecycleMismatch,         // interface 必须是 lifecycle=none 且没有槽位
        ContractRequirementMismatch,        // 契约条数与需求组数不一致
        ContractCandidateMismatch,          // 逐候选修订记录与候选名集合不是一一对应
        DuplicatePlugin,                    // catalog 内同名插件
        NameHashCollision,                  // 名字不同但 64 位 ID 相同（碰撞模拟或真碰撞）
        TargetMacroConflict,                // 两个插件的 target 归一化后映射到同一个 ID_USE_PLUGIN_ 宏
        NamespaceConflict,                  // 两个插件声明了同一个精确 ASCII 命名空间

        // ---- 阶段 2：catalog 全量候选引用（AssemblyPhase::CatalogBinding） ----
        UnknownCandidate,                   // 候选名不在完整 catalog 中（未选中的插件也校验）
        CandidateCapabilityMismatch,        // 候选没有声明提供该需求的能力（白名单必须与能力一致）

        // ---- 阶段 3：选中集合与唯一绑定（AssemblyPhase::Selection） ----
        TooManyActive,                      // 选中插件数超过 128
        InvalidActive,                      // 选中身份本身非法
        UnknownActive,                      // 选中集合出现 catalog 之外的插件
        DuplicateActive,                    // 选中集合重复出现同一插件
        MissingProvider,                    // 某需求在选中集合内 0 个候选
        AmbiguousProvider,                  // 某需求在选中集合内 >= 2 个候选
        RevisionMismatch,                   // 唯一候选的接口修订不在契约区间内

        // ---- 阶段 4：能力与槽位一致性（AssemblyPhase::Ownership） ----
        DuplicateCapability,                // 同一能力在选中集合里出现两个提供者（即使无人使用）
        SlotConflict,                       // 不同能力占用同一个服务槽位

        // ---- 阶段 5：固定图（AssemblyPhase::Graph） ----
        Cycle,                              // 选中后的固定图存在环

        // ---- 阶段 6：构建证据（AssemblyPhase::Evidence） ----
        EvidenceSelectionMismatch,          // 证据的选中名字集合与 C++ 推导不一致
        EvidenceTargetMismatch,             // 证据的 CMake 目标名与声明的 target 不一致
        EvidenceMacroMismatch,              // 证据的身份宏与 target 归一化结果不一致
        EvidenceIdentityMismatch,           // 证据记录的插件身份（64 位 ID）与推导不一致
        EvidenceProviderMismatch,           // 证据的逐需求 provider 与推导不一致
        EvidenceEdgeMismatch,               // 证据的真实链接边与推导不一致
        EvidenceSlotMismatch,               // 证据的槽位归属与推导不一致

        // ---- 阶段 7：运行期绑定元数据（AssemblyPhase::RuntimeMetadata） ----
        RuntimeBindingMismatch,             // 运行期绑定记录的插件身份/接口修订与计划不一致

        MaxErrors
    };

    /**
     *  @brief 错误所处阶段。阶段优先级固定，用于证明“低阶段错误不会被高阶段掩盖”。
     */
    enum class AssemblyPhase : uint8_t
    {
        None = 0,           // 无错误
        Declaration,        // 格式 / 身份 / 契约 / 容量
        CatalogBinding,     // catalog 全量候选引用
        Selection,          // 选中集合与逐需求唯一绑定
        Ownership,          // 能力与槽位一致性
        Graph,              // 去重边、拓扑序、环
        Evidence,           // 构建系统派生的证据
        RuntimeMetadata,    // 运行期绑定身份/修订元数据
        MaxPhases
    };

    /**
     *  @brief 错误类别的稳定文本（诊断与测试输出用）
     *  @param code 错误类别
     *  @return 形如 "AmbiguousProvider" 的静态字符串
     */
    constexpr std::string_view plugin_error_name(PluginError code) noexcept
    {
        switch (code)
        {
            case PluginError::None:                      return "None";
            case PluginError::InvalidName:               return "InvalidName";
            case PluginError::InvalidTargetName:         return "InvalidTargetName";
            case PluginError::InvalidNamespace:          return "InvalidNamespace";
            case PluginError::InvalidDeclaration:        return "InvalidDeclaration";
            case PluginError::SchemaMismatch:            return "SchemaMismatch";
            case PluginError::TooManyCatalog:            return "TooManyCatalog";
            case PluginError::TooManyRequirements:       return "TooManyRequirements";
            case PluginError::TooManyCandidates:         return "TooManyCandidates";
            case PluginError::TooManyProvides:           return "TooManyProvides";
            case PluginError::EmptyRequirementGroup:     return "EmptyRequirementGroup";
            case PluginError::DuplicateCandidate:        return "DuplicateCandidate";
            case PluginError::SelfDependency:            return "SelfDependency";
            case PluginError::InvalidCapability:         return "InvalidCapability";
            case PluginError::InvalidRevision:           return "InvalidRevision";
            case PluginError::InvalidRevisionRange:      return "InvalidRevisionRange";
            case PluginError::InvalidSlot:               return "InvalidSlot";
            case PluginError::InterfaceLifecycleMismatch: return "InterfaceLifecycleMismatch";
            case PluginError::ContractRequirementMismatch: return "ContractRequirementMismatch";
            case PluginError::ContractCandidateMismatch: return "ContractCandidateMismatch";
            case PluginError::DuplicatePlugin:           return "DuplicatePlugin";
            case PluginError::NameHashCollision:         return "NameHashCollision";
            case PluginError::TargetMacroConflict:       return "TargetMacroConflict";
            case PluginError::NamespaceConflict:         return "NamespaceConflict";
            case PluginError::UnknownCandidate:          return "UnknownCandidate";
            case PluginError::CandidateCapabilityMismatch: return "CandidateCapabilityMismatch";
            case PluginError::TooManyActive:             return "TooManyActive";
            case PluginError::InvalidActive:             return "InvalidActive";
            case PluginError::UnknownActive:             return "UnknownActive";
            case PluginError::DuplicateActive:           return "DuplicateActive";
            case PluginError::MissingProvider:           return "MissingProvider";
            case PluginError::AmbiguousProvider:         return "AmbiguousProvider";
            case PluginError::RevisionMismatch:          return "RevisionMismatch";
            case PluginError::DuplicateCapability:       return "DuplicateCapability";
            case PluginError::SlotConflict:              return "SlotConflict";
            case PluginError::Cycle:                     return "Cycle";
            case PluginError::EvidenceSelectionMismatch: return "EvidenceSelectionMismatch";
            case PluginError::EvidenceTargetMismatch:    return "EvidenceTargetMismatch";
            case PluginError::EvidenceMacroMismatch:     return "EvidenceMacroMismatch";
            case PluginError::EvidenceIdentityMismatch:  return "EvidenceIdentityMismatch";
            case PluginError::EvidenceProviderMismatch:  return "EvidenceProviderMismatch";
            case PluginError::EvidenceEdgeMismatch:      return "EvidenceEdgeMismatch";
            case PluginError::EvidenceSlotMismatch:      return "EvidenceSlotMismatch";
            case PluginError::RuntimeBindingMismatch:    return "RuntimeBindingMismatch";
            case PluginError::MaxErrors:                 return "MaxErrors";
        }
        return "Unknown";
    }

    /**
     *  @brief 阶段文本
     *  @param phase 阶段
     *  @return 形如 "Selection" 的静态字符串
     */
    constexpr std::string_view assembly_phase_name(AssemblyPhase phase) noexcept
    {
        switch (phase)
        {
            case AssemblyPhase::None:            return "None";
            case AssemblyPhase::Declaration:     return "Declaration";
            case AssemblyPhase::CatalogBinding:  return "CatalogBinding";
            case AssemblyPhase::Selection:       return "Selection";
            case AssemblyPhase::Ownership:       return "Ownership";
            case AssemblyPhase::Graph:           return "Graph";
            case AssemblyPhase::Evidence:        return "Evidence";
            case AssemblyPhase::RuntimeMetadata: return "RuntimeMetadata";
            case AssemblyPhase::MaxPhases:       return "MaxPhases";
        }
        return "Unknown";
    }

    /**
     *  @brief 插件编译期身份（计划书 5.2）
     *  @note 名字复制进定长拥有存储，因此不同长度的名字可以放进同一类型数组，复制/移动不引用原对象；
     *        prefix0/prefix1 只是比较加速键，**不是**相等判据，完整比较必须再逐字节确认。
     */
    struct PluginIdentity
    {
        char        name[kNameCapacity] = {};       // 拥有存储的名字（含末尾 NUL）
        uint64_t    prefix0             = 0;        // 前 8 字节（小端拼接，不足补 0）
        uint64_t    prefix1             = 0;        // 第 8..15 字节
        uint64_t    id                  = 0;        // FNV-1a 64 插件 ID
        uint32_t    length              = 0;        // 名字字节数（不含末尾 NUL）
        bool        is_valid            = false;    // 名字格式是否通过校验

        /**
         *  @brief 名字视图
         *  @return 指向本对象拥有存储的视图；复制本对象不会让视图指向旧对象，但视图本身是临时量
         */
        constexpr std::string_view view() const noexcept
        {
            return { name, length };
        }
    };

    namespace detail
    {
        // ---- 字符类别位图（表驱动） ----
        // 常量求值把每一次子表达式都计入操作数：逐字符的 7 次分支比较约 19 个操作/字符，
        // 而一次查表 + 位测试约 4 个操作/字符。名字格式校验在满规模输入上要跑十万次字符，
        // 因此这里用 256 项类别表把整趟扫描的成本压下来，语义与逐字符比较完全一致。
        inline constexpr uint8_t kCharAlpha     = 0x01;     // A-Z a-z
        inline constexpr uint8_t kCharDigit     = 0x02;     // 0-9
        inline constexpr uint8_t kCharNameHead  = 0x04;     // 插件名/目标名首字符：字母
        inline constexpr uint8_t kCharNameTail  = 0x08;     // 插件名/目标名其余：字母数字与 . _ -
        inline constexpr uint8_t kCharSpaceHead = 0x10;     // 命名空间段首字符：字母与 _
        inline constexpr uint8_t kCharSpaceTail = 0x20;     // 命名空间段内字符：字母数字与 _

        inline constexpr uint64_t kFnvOffsetBasis = 14695981039346656037ull;
        inline constexpr uint64_t kFnvPrime       = 1099511628211ull;

        /**
         *  @brief 生成 256 项字符类别表
         *  @return 下标是字节值，值是 kChar* 位掩码
         *  @note 表只在常量求值期生成一次，不进入运行期产物；非 ASCII 字节一律 0（非法）。
         */
        consteval std::array<uint8_t, 256> make_char_flags() noexcept
        {
            std::array<uint8_t, 256> table{};
            for (size_t index = 0; index < 256; ++index)
            {
                const char value = static_cast<char>(index);
                uint8_t    flags = 0;
                if ((value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z'))
                {
                    flags = static_cast<uint8_t>(kCharAlpha | kCharNameHead | kCharNameTail | kCharSpaceHead | kCharSpaceTail);
                }
                else if (value >= '0' && value <= '9')
                {
                    flags = static_cast<uint8_t>(kCharDigit | kCharNameTail | kCharSpaceTail);
                }
                else if (value == '.' || value == '-')
                {
                    flags = kCharNameTail;
                }
                else if (value == '_')
                {
                    flags = static_cast<uint8_t>(kCharNameTail | kCharSpaceHead | kCharSpaceTail);
                }
                table[index] = flags;
            }
            return table;
        }

        inline constexpr std::array<uint8_t, 256> kCharFlags = make_char_flags();

        /**
         *  @brief 生成身份宏归一化映射表：小写转大写，非 [A-Z0-9_] 一律映射成下划线
         *  @note 与构建原语 `ID_USE_PLUGIN_<TARGET_ID>` 的口径一致；逐字符只剩一次查表。
         */
        consteval std::array<char, 256> make_macro_chars() noexcept
        {
            std::array<char, 256> table{};
            for (size_t index = 0; index < 256; ++index)
            {
                char value = static_cast<char>(index);
                if (value >= 'a' && value <= 'z')
                {
                    value = static_cast<char>(value - 'a' + 'A');
                }
                else if (!((value >= 'A' && value <= 'Z') || (value >= '0' && value <= '9') || value == '_'))
                {
                    value = '_';
                }
                table[index] = value;
            }
            return table;
        }

        inline constexpr std::array<char, 256> kMacroChars = make_macro_chars();

        /**
         *  @brief 单个字节的类别位
         *  @param value 字符
         *  @return kChar* 位掩码；非 ASCII 字节为 0
         */
        constexpr uint8_t char_flags(char value) noexcept
        {
            return kCharFlags[static_cast<unsigned char>(value)];
        }

        constexpr bool is_name_head(char value) noexcept
        {
            return (char_flags(value) & kCharNameHead) != 0;
        }

        constexpr bool is_name_tail(char value) noexcept
        {
            return (char_flags(value) & kCharNameTail) != 0;
        }

        constexpr bool is_namespace_head(char value) noexcept
        {
            return (char_flags(value) & kCharSpaceHead) != 0;
        }

        constexpr bool is_namespace_tail(char value) noexcept
        {
            return (char_flags(value) & kCharSpaceTail) != 0;
        }

        /**
         *  @brief 插件名/能力名格式校验：非空、<= 96 字节、首字符字母、其余为字母数字与 . _ -
         *  @param text 名字
         *  @return 合法为真；ASCII 大小写敏感，不含 NUL 与空白
         */
        constexpr bool is_valid_name(std::string_view text) noexcept
        {
            if (text.empty() || text.size() > kMaxNameBytes || !is_name_head(text[0]))
            {
                return false;
            }
            for (size_t i = 1; i < text.size(); ++i)
            {
                if (!is_name_tail(text[i]))
                {
                    return false;
                }
            }
            return true;
        }

        /**
         *  @brief CMake 目标名格式校验：非空、<= 96 字节、首字符字母、其余为字母数字与 . _ -
         */
        constexpr bool is_valid_target(std::string_view text) noexcept
        {
            if (text.empty() || text.size() > kMaxTargetBytes || !is_name_head(text[0]))
            {
                return false;
            }
            for (size_t i = 1; i < text.size(); ++i)
            {
                if (!is_name_tail(text[i]))
                {
                    return false;
                }
            }
            return true;
        }

        /**
         *  @brief C++ 命名空间格式校验：`段(::段)*`，每段首字符为字母或下划线，其余为字母数字与下划线
         */
        constexpr bool is_valid_namespace(std::string_view text) noexcept
        {
            if (text.empty() || text.size() > kMaxNamespaceBytes)
            {
                return false;
            }
            size_t cursor = 0;
            while (true)
            {
                if (cursor >= text.size() || !is_namespace_head(text[cursor]))
                {
                    return false;
                }
                ++cursor;
                while (cursor < text.size() && is_namespace_tail(text[cursor]))
                {
                    ++cursor;
                }
                if (cursor == text.size())
                {
                    return true;
                }
                if (text[cursor] != ':' || cursor + 1 >= text.size() || text[cursor + 1] != ':')
                {
                    return false;
                }
                cursor += 2;
            }
        }

        /**
         *  @brief 归一化身份宏的第 index 个字节（流式：不落缓冲）
         *  @param target CMake 目标名
         *  @param index 归一化后的字节下标，必须小于 kMacroLength(target)
         *  @return 该位置上的宏字节
         *  @note 前缀 "ID_USE_PLUGIN_" 与归一化后的 target 拼成同一个流，因此比较两端都不必先落 128 字节缓冲。
         */
        constexpr char identity_macro_byte(std::string_view target, size_t index) noexcept
        {
            constexpr char kPrefix[] = "ID_USE_PLUGIN_";
            constexpr size_t kPrefixLength = sizeof(kPrefix) - 1;
            if (index < kPrefixLength)
            {
                return kPrefix[index];
            }
            return kMacroChars[static_cast<unsigned char>(target[index - kPrefixLength])];
        }

        /**
         *  @brief 归一化身份宏的长度
         *  @param target CMake 目标名
         *  @return 长度；超过 kMacroCapacity 时返回 0 表示不可用
         */
        constexpr size_t identity_macro_length(std::string_view target) noexcept
        {
            constexpr size_t kPrefixLength = sizeof("ID_USE_PLUGIN_") - 1;
            const size_t     length        = kPrefixLength + target.size();
            return length <= kMacroCapacity ? length : 0;
        }

        /**
         *  @brief 目标名归一化后的身份宏是否正好等于给定的宏文本
         *  @param target CMake 目标名
         *  @param macro 构建证据里记录的身份宏全名（不含 "=1"）
         *  @return 完全一致为真
         *  @note 长度先判，再逐字节流式比较，不再为每条证据落一个 kMacroCapacity 缓冲。
         */
        constexpr bool identity_macro_matches(std::string_view target, std::string_view macro) noexcept
        {
            const size_t length = identity_macro_length(target);
            if (length == 0 || length != macro.size())
            {
                return false;
            }
            for (size_t i = 0; i < length; ++i)
            {
                if (identity_macro_byte(target, i) != macro[i])
                {
                    return false;
                }
            }
            return true;
        }

        /**
         *  @brief 两个 target 归一化后是否映射到同一个身份宏（冲突检测用）
         *  @note 长度先判，相等才逐字节流式比较；不构造任何临时缓冲。
         */
        constexpr bool identity_macro_equal(std::string_view left, std::string_view right) noexcept
        {
            const size_t length = identity_macro_length(left);
            if (length == 0 || length != identity_macro_length(right))
            {
                return false;
            }
            for (size_t i = 0; i < length; ++i)
            {
                if (identity_macro_byte(left, i) != identity_macro_byte(right, i))
                {
                    return false;
                }
            }
            return true;
        }

        /**
         *  @brief 归一化身份宏的 FNV-1a 64（冲突索引表的键）
         *  @param target CMake 目标名
         *  @return 归一化文本的 64 位哈希；长度超限时返回 kFnvOffsetBasis 之外的固定值，保证仍可与 0 区分
         *  @note 只是一次流式扫描，不落缓冲：身份宏冲突表因此不必为每个条目准备 128 字节临时数组。
         */
        constexpr uint64_t identity_macro_hash(std::string_view target) noexcept
        {
            const size_t   length = identity_macro_length(target);
            uint64_t       hash   = kFnvOffsetBasis;
            const uint64_t effective_length = length == 0 ? target.size() : length;
            for (size_t i = 0; i < effective_length; ++i)
            {
                hash ^= static_cast<unsigned char>(identity_macro_byte(target, i));
                hash *= kFnvPrime;
            }
            return hash;
        }

        /**
         *  @brief 从字符缓冲建立身份：格式校验、名字复制、16 字节前缀、FNV-1a 64 单趟完成
         *  @param text 字符起始地址，长度至少 length 字节
         *  @param length 名字字节数（不含末尾 NUL）
         *  @return 合法时 is_valid 为真且名字已复制；非法时返回默认身份（is_valid 为假），不 abort、不抛异常
         *  @note 内部构造入口，生产 register_plugin 只提供 `const char (&)[N]` 形态；
         *        FNV 在这里内联展开是为了避免第二趟遍历，算法与 ID::hash_fnv_1a64 完全一致（见下方 static_assert）。
         */
        constexpr PluginIdentity make_identity(const char* text, size_t length) noexcept
        {
            PluginIdentity identity{};
            if (text == nullptr || length == 0 || length > kMaxNameBytes || !is_name_head(text[0]))
            {
                return identity;
            }
            // 首字符已单独判过（首字符集合是尾字符集合的子集），循环里不再重复测试 i != 0
            uint64_t hash = kFnvOffsetBasis;
            for (size_t i = 0; i < length; ++i)
            {
                const char character = text[i];
                if (!is_name_tail(character))
                {
                    return identity;
                }
                const unsigned char byte = static_cast<unsigned char>(character);
                identity.name[i] = character;
                hash ^= byte;
                hash *= kFnvPrime;
                if (i < 8)
                {
                    identity.prefix0 |= static_cast<uint64_t>(byte) << (8 * i);
                }
                else if (i < 16)
                {
                    identity.prefix1 |= static_cast<uint64_t>(byte) << (8 * (i - 8));
                }
            }
            identity.name[length] = '\0';
            identity.length      = static_cast<uint32_t>(length);
            identity.id          = hash;
            identity.is_valid    = true;
            return identity;
        }

        /**
         *  @brief 为“字符数组末尾没有 NUL”的输入构造结构化无效身份
         *  @param text 字符数组起始地址
         *  @param length 数组实际字节数（包含最后一个非 NUL 字符）
         *  @return is_valid 为假的身份；name 拥有存储保留最多 kMaxNameBytes 个输入字节，
         *          **不丢弃最后一个字符**（除非数组本身超过名字容量，此时身份必然非法）
         *  @note 非终止数组不是合法身份，不能通过 N-1 静默截掉最后一个字符；
         *        id/prefix 保持 0，避免下游把无效身份当作可比较的真实哈希。
         */
        constexpr PluginIdentity make_unterminated_identity(const char* text, size_t length) noexcept
        {
            PluginIdentity identity{};
            if (text == nullptr)
            {
                return identity;
            }
            const size_t copied = length > kMaxNameBytes ? kMaxNameBytes : length;
            for (size_t i = 0; i < copied; ++i)
            {
                identity.name[i] = text[i];
            }
            identity.name[copied] = '\0';
            identity.length       = static_cast<uint32_t>(copied);
            identity.is_valid     = false;
            return identity;
        }

        /**
         *  @brief `const char (&)[N]` 形态的身份构造：先验证末尾 NUL，再走正常名字解析
         *  @param text 编译期字符数组
         *  @return 合法字面量返回有效身份；末尾不是 NUL 时返回结构化无效身份
         *  @note register_plugin 只走这个入口；`N - 1` 只在确认 text[N - 1] == '\0' 后使用，
         *        因此不会把最后一个字符静默截掉。
         */
        template <size_t N>
        constexpr PluginIdentity make_identity_literal(const char (&text)[N]) noexcept
        {
            if (text[N - 1] != '\0')
            {
                return make_unterminated_identity(text, N);
            }
            return make_identity(text, N - 1);
        }

        /**
         *  @brief 名字字节（尚未构造身份）与已有身份做**完整**比较
         *  @param self 已构造的身份（拥有存储）
         *  @param bytes 名字字节起始地址
         *  @param length 名字字节数
         *  @param id 名字的 FNV-1a 64
         *  @param prefix0 前 8 字节
         *  @param prefix1 第 8..15 字节
         *  @return 名字完全相同时为真
         *  @note 先比标量键，键全同才逐字节确认；“哈希与前缀都相同但名字不同”必须判为不相等。
         */
        constexpr bool name_equals(const PluginIdentity& self, const char* bytes, size_t length,
                                   uint64_t id, uint64_t prefix0, uint64_t prefix1) noexcept
        {
            if (self.length != length || self.id != id || self.prefix0 != prefix0 || self.prefix1 != prefix1)
            {
                return false;
            }
            for (size_t i = 16; i < length; ++i)
            {
                if (self.name[i] != bytes[i])
                {
                    return false;
                }
            }
            return true;
        }

        /**
         *  @brief 已存名字视图与候选字节在“标量键（长度 / ID / 两个前缀字）已相等”前提下的满名确认
         *  @param stored catalog 名字视图（长度必须等于 length，由调用方先比过标量键）
         *  @param bytes 待比较的名字字节
         *  @param length 名字字节数
         *  @return 第 16 字节之后全部相同为真
         *  @note 前 16 字节已由 (prefix0, prefix1) 覆盖，这里只补剩余部分；长度 <= 16 时调用方直接跳过。
         */
        constexpr bool name_tail_equals(std::string_view stored, const char* bytes, size_t length) noexcept
        {
            for (size_t i = 16; i < length; ++i)
            {
                if (stored[i] != bytes[i])
                {
                    return false;
                }
            }
            return true;
        }

        /**
         *  @brief 名字按无符号字节序严格小于（Kahn 的确定性 tie-break 依据）
         */
        constexpr bool name_less(const PluginIdentity& left, const PluginIdentity& right) noexcept
        {
            const size_t shorter = left.length < right.length ? left.length : right.length;
            for (size_t i = 0; i < shorter; ++i)
            {
                if (left.name[i] != right.name[i])
                {
                    return static_cast<unsigned char>(left.name[i]) < static_cast<unsigned char>(right.name[i]);
                }
            }
            return left.length < right.length;
        }

        /**
         *  @brief 已构造身份与名字字节按无符号字节序比较（canonical 有序数组的二分查找用）
         *  @param identity 已构造身份（含有存储的名字）
         *  @param text 外部名字视图
         *  @return identity 的名字字节序严格小于 text 时为真
         *  @note 只用于在**已按该顺序排好**的数组里定位，不承担相等判定；相等判定仍由完整视图比较完成。
         */
        constexpr bool identity_less_than_view(const PluginIdentity& identity, std::string_view text) noexcept
        {
            const size_t shorter = identity.length < text.size() ? identity.length : text.size();
            for (size_t i = 0; i < shorter; ++i)
            {
                if (identity.name[i] != text[i])
                {
                    return static_cast<unsigned char>(identity.name[i]) < static_cast<unsigned char>(text[i]);
                }
            }
            return identity.length < text.size();
        }

        /**
         *  @brief FNV-1a 续算，用于把规范化名字与绑定关系折进确定性指纹
         */
        constexpr uint64_t hash_mix(uint64_t hash, uint64_t value) noexcept
        {
            hash ^= value;
            hash *= kFnvPrime;
            return hash;
        }

        /**
         *  @brief 把一段字节折进 FNV-1a 64
         */
        constexpr uint64_t hash_bytes(uint64_t hash, std::string_view text) noexcept
        {
            for (size_t i = 0; i < text.size(); ++i)
            {
                hash = hash_mix(hash, static_cast<unsigned char>(text[i]));
            }
            return hash;
        }

        /**
         *  @brief 不小于 value 的最小 2 的幂（开放寻址表容量，便于用掩码取模）
         *  @param value 下界，0 与 1 都返回 1
         *  @return 2 的幂
         */
        constexpr size_t next_pow2(size_t value) noexcept
        {
            size_t result = 1;
            while (result < value)
            {
                result <<= 1;
            }
            return result;
        }
    } // namespace detail

    // 身份构造必须与 ID::hash_fnv_1a64 使用同一算法，否则插件 ID 与旧描述符 ID 会分叉
    static_assert(detail::make_identity("ID.Math.Simd", 12).id == hash_fnv_1a64("ID.Math.Simd"),
                  "PluginCompileTime 身份哈希必须与 ID::hash_fnv_1a64 一致");
    static_assert(detail::make_identity("ID.A", 4).is_valid, "合法名字必须构造出有效身份");
    static_assert(!detail::make_identity("1D.A", 4).is_valid, "首字符必须是字母");
    static_assert(detail::is_valid_namespace("ID::Physics") && detail::is_valid_namespace("ID"),
                  "命名空间格式校验必须接受单段与多段");

    /**
     *  @brief 一个插件的不可变依赖声明（计划书 5.3）
     *  @tparam GroupCount 需求组数（外层 AND）
     *  @tparam CandidateCount 候选名总数（各行内层 OR 的叶子总数）
     *  @note group_begin[i]..group_begin[i+1] 是第 i 组在候选表里的区间；候选使用紧凑拥有存储（SoA）：
     *        逐候选只保留 (长度, 池内偏移, ID, 两个前缀字) 标量，名字字节写进 candidate_pool，
     *        因此复制/移动声明不会留下悬空 view，也不会退化成“只存哈希”而丢失完整名比较能力。
     */
    template <size_t GroupCount, size_t CandidateCount>
    struct PluginDeclaration
    {
        static inline constexpr size_t kGroupCapacity     = GroupCount;
        static inline constexpr size_t kCandidateCapacity = CandidateCount;
        static inline constexpr size_t kCandidateSlots    = CandidateCount > 0 ? CandidateCount : 1;
        static inline constexpr size_t kPoolBytes         = CandidateCount > 0 ? CandidateCount * kMaxNameBytes : 1;

        PluginIdentity identity{};                                                  // 所属插件身份
        PluginError    error           = PluginError::None;                         // 声明期第一条错误
        uint16_t       group_count     = 0;                                         // 实际需求组数
        uint16_t       candidate_count = 0;                                         // 实际候选数
        uint16_t       error_group     = 0;                                         // 出错的组下标
        uint16_t       error_candidate = 0;                                         // 出错的候选下标
        uint16_t       group_begin[GroupCount + 1]                     = {};       // 每组在候选表中的区间起点
        uint16_t       candidate_length[kCandidateSlots]               = {};       // 候选名字字节数
        uint32_t       candidate_offset[kCandidateSlots + 1]           = {};       // 候选名字在池内起点（末位=已用字节）
        uint64_t       candidate_id[kCandidateSlots]                   = {};       // 候选名字 FNV-1a 64
        uint64_t       candidate_prefix0[kCandidateSlots]              = {};       // 候选前 8 字节
        uint64_t       candidate_prefix1[kCandidateSlots]              = {};       // 候选第 8..15 字节
        char           candidate_pool[kPoolBytes]                      = {};       // 候选名字拥有存储

        /**
         *  @brief 第 index 个候选的名字视图
         *  @param index 候选下标，必须小于 candidate_count
         *  @return 指向本声明拥有存储的名字视图；随声明一起复制，不会悬空
         */
        constexpr std::string_view candidate_view(size_t index) const noexcept
        {
            return { candidate_pool + candidate_offset[index], candidate_length[index] };
        }
    };

    /**
     *  @brief 逐候选的接口修订要求（与需求组内的候选逐一对应）
     */
    struct CandidateRevision
    {
        std::string_view name         = {};         // 候选取名（必须与组内候选精确一致）
        uint32_t         min_revision = 0;          // 允许的最小接口修订（>= 1）
        uint32_t         max_revision = 0;          // 允许的最大接口修订（>= min）
    };

    /**
     *  @brief 一个需求组的能力契约
     *  @note revision_begin/count 指向 PluginMetadata::revisions 的扁平区间；
     *        契约条数必须等于需求组数，区间必须与对应组的候选名集合一一对应。
     */
    struct RequirementContract
    {
        std::string_view capability     = {};       // 本组需求的能力名（如 "ID.Math"）
        uint16_t         revision_begin = 0;        // 扁平修订表起点
        uint16_t         revision_count = 0;        // 修订记录数（必须等于该组候选数）
    };

    /**
     *  @brief 一个插件提供的能力
     *  @note api_revision 是这个实现自己的**接口修订**，不是插件发布版本；
     *        slot 为 kNoSlot 表示纯头文件能力（没有运行期函数表）。
     *        一个插件可以声明多条 provides（不同能力、不同槽位），数量由 PluginCatalogEntry 的 P 给出。
     */
    struct Provision
    {
        std::string_view capability   = {};
        uint32_t         api_revision = 0;
        int16_t          slot         = kNoSlot;
    };

    /**
     *  @brief 插件种类
     */
    enum class PluginKind : uint8_t
    {
        Module    = 0,      // 有实现模块，可以有服务槽位与生命周期回调
        Interface = 1       // 纯头文件插件：必须 lifecycle=none 且没有槽位
    };

    /**
     *  @brief 插件生命周期模式
     */
    enum class PluginLifecycle : uint8_t
    {
        Standard = 0,       // 使用固定的四个回调
        None     = 1        // 无生命周期回调；interface 必须同时无槽位，module 的槽位由 P3 runtime 处理
    };

    /**
     *  @brief 一个插件在 catalog 中的元数据（计划书 4.3 的 C++ 形态）
     *  @tparam GroupCount 需求组数（= 契约条数）
     *  @tparam RevisionCapacity 扁平修订记录容量（正常等于候选总数）
     *  @tparam ProvidesCapacity 该插件 provides 数组容量；由 catalog 条目类型显式给出，
     *         不是全局固定 4 条。provides_count 必须 <= 该容量，超出时结构化诊断，不静默截断。
     *  @note target/cpp_namespace 只用于格式与身份宏映射校验；version 是插件发布版本，
     *        **不参与**接口兼容判定，接口修订只认 Provision::api_revision。
     */
    template <size_t GroupCount, size_t RevisionCapacity, size_t ProvidesCapacity = 1>
    struct PluginMetadata
    {
        static inline constexpr size_t kContractSlots    = GroupCount > 0 ? GroupCount : 1;
        static inline constexpr size_t kRevisionSlots    = RevisionCapacity > 0 ? RevisionCapacity : 1;
        static inline constexpr size_t kProvidesCapacity = ProvidesCapacity;
        static inline constexpr size_t kProvidesSlots    = ProvidesCapacity > 0 ? ProvidesCapacity : 1;

        std::string_view    target          = {};                        // CMake 目标名
        std::string_view    cpp_namespace   = {};                        // C++ 命名空间（如 "ID::Physics"）
        PluginKind          kind            = PluginKind::Module;
        PluginLifecycle     lifecycle       = PluginLifecycle::None;
        uint32_t            version         = 1;                         // 插件发布版本，不当作 ABI 修订
        uint16_t            contract_count  = 0;                         // 必须等于需求组数
        uint16_t            provides_count  = 0;                         // 实际 provides 条数，<= kProvidesCapacity
        RequirementContract contracts[kContractSlots]  = {};
        CandidateRevision   revisions[kRevisionSlots]  = {};
        Provision           provides[kProvidesSlots]   = {};
    };

    /**
     *  @brief catalog 的一个条目：拥有存储的依赖声明 + 能力/槽位/接口修订元数据
     *  @tparam GroupCount 需求组数
     *  @tparam CandidateCount 候选总数
     *  @tparam ProvidesCapacity 该插件 provides 数组长度；多能力插件必须显式给出，例如 PluginCatalogEntry<G, C, 3>
     *  @note 元数据容量与声明规模严格一致，不为短板插件预留 32 组 x 16 候选的最坏情况；
     *        Shape 会从全部条目的 ProvidesCapacity 求和推导 result 的 provides 容量。
     */
    template <size_t GroupCount, size_t CandidateCount, size_t ProvidesCapacity = 1>
    struct PluginCatalogEntry
    {
        using DeclarationType = PluginDeclaration<GroupCount, CandidateCount>;
        using MetadataType    = PluginMetadata<GroupCount, CandidateCount, ProvidesCapacity>;

        static inline constexpr size_t kProvidesCapacity = ProvidesCapacity;

        DeclarationType declaration = {};
        MetadataType    metadata    = {};
    };

    namespace detail
    {
        template <typename Entry>
        struct entry_provides_capacity
        {
            static inline constexpr size_t value = Entry::MetadataType::kProvidesCapacity;
        };

        template <typename Catalog>
        struct catalog_provision_capacity;

        template <typename... Entry>
        struct catalog_provision_capacity<std::tuple<Entry...>>
        {
            static inline constexpr size_t value = (entry_provides_capacity<Entry>::value + ... + 0);
        };
    } // namespace detail

    // ---------- 构建证据（由构建系统从同一份声明派生，供 C++ 侧交叉校验） ----------

    /**
     *  @brief 选中项证据：插件名、CMake 目标、身份宏、编译身份
     */
    struct SelectedEvidence
    {
        std::string_view name      = {};        // 插件精确名
        std::string_view target    = {};        // CMake 目标名
        std::string_view macro     = {};        // 身份宏全名，如 "ID_USE_PLUGIN_ID_MATH_SIMD"（不含 =1）
        uint64_t         plugin_id = 0;         // 编译期身份（必须等于推导出的 FNV-1a 64）
    };

    /**
     *  @brief 逐需求 provider 证据
     */
    struct BindingEvidence
    {
        std::string_view dependent   = {};      // 下游插件名
        std::string_view provider    = {};      // 被选中的唯一提供者名
        uint16_t         requirement = 0;       // 需求组下标
    };

    /**
     *  @brief 真实链接边证据（上游 -> 下游）
     */
    struct EdgeEvidence
    {
        std::string_view from = {};             // 上游插件名
        std::string_view to   = {};             // 下游插件名
    };

    /**
     *  @brief 服务槽位归属证据
     */
    struct SlotEvidence
    {
        std::string_view owner      = {};       // 占槽插件名
        std::string_view capability = {};       // 该插件发布的函数表能力名
        int16_t          slot       = kNoSlot;  // 0..63
    };

    /**
     *  @brief 运行期绑定元数据证据：某个插件为某个能力记录的编译身份与接口修订
     *  @note 本阶段只做结构比对，不生成 PluginRuntime（计划书 P3）。
     */
    struct RuntimeBindingEvidence
    {
        std::string_view name         = {};     // 插件名
        std::string_view capability   = {};     // 该记录对应的能力名
        uint64_t         plugin_id    = 0;      // 运行期记录的插件身份
        uint32_t         api_revision = 0;      // 运行期记录的接口修订
    };

    /**
     *  @brief 组装输入的容量形状（编译期类型参数）
     *  @tparam CatalogType 完整 catalog 的 tuple 类型（元素为 PluginCatalogEntry）
     *  @tparam ActiveCapacity 本产品选中插件数（= selected 数组长度）
     *  @tparam SelectedEvidenceCapacity 选中证据容量
     *  @tparam BindingEvidenceCapacity 逐需求 provider 证据容量
     *  @tparam EdgeEvidenceCapacity 链接边证据容量
     *  @tparam SlotEvidenceCapacity 槽位证据容量（产品级槽位唯一，最多 64 条）
     *  @tparam RuntimeEvidenceCapacity 运行期绑定元数据容量，缺省由 catalog 全量 provides 容量推导
     *  @note 容量必须由生成器已知的数组长度给出：consteval 不会把普通形参计数变成模板实参。
     *        provides 容量不是每插件固定 4：它等于每个 PluginCatalogEntry 的 ProvidesCapacity 之和。
     */
    template <typename CatalogType,
              size_t ActiveCapacity,
              size_t SelectedEvidenceCapacity,
              size_t BindingEvidenceCapacity,
              size_t EdgeEvidenceCapacity,
              size_t SlotEvidenceCapacity    = kMaxSlots,
              size_t RuntimeEvidenceCapacity = detail::catalog_provision_capacity<CatalogType>::value>
    struct AssemblyShape
    {
        using CatalogTypeAlias = CatalogType;

        static inline constexpr size_t kCatalogCapacity          = std::tuple_size_v<CatalogType>;
        static inline constexpr size_t kActiveCapacity           = ActiveCapacity;
        static inline constexpr size_t kSelectedEvidenceCapacity = SelectedEvidenceCapacity;
        static inline constexpr size_t kBindingEvidenceCapacity  = BindingEvidenceCapacity;
        static inline constexpr size_t kEdgeEvidenceCapacity     = EdgeEvidenceCapacity;
        static inline constexpr size_t kSlotEvidenceCapacity     = SlotEvidenceCapacity;
        static inline constexpr size_t kRuntimeEvidenceCapacity  = RuntimeEvidenceCapacity;
        static inline constexpr size_t kCatalogProvisionCapacity = detail::catalog_provision_capacity<CatalogType>::value;
        static inline constexpr size_t kProvisionSlots = kCatalogProvisionCapacity > 0 ? kCatalogProvisionCapacity : 1;
        // 能力唯一性索引表：2 倍 provides 容量（负载因子 <= 0.5）并向上取整到 2 的幂
        static inline constexpr size_t kCapabilitySlotCount = detail::next_pow2(kProvisionSlots * 2);
        static inline constexpr uint16_t kCapabilitySlotMask = static_cast<uint16_t>(kCapabilitySlotCount - 1);

        static_assert(kCatalogProvisionCapacity <= static_cast<size_t>(kNoIndex),
                      "catalog 全量 provides 容量超过 uint16_t 索引范围");
        static_assert(kCapabilitySlotCount <= static_cast<size_t>(kNoIndex),
                      "能力唯一性索引表容量超过 uint16_t 索引范围");
        static_assert((kCapabilitySlotCount & (kCapabilitySlotCount - 1)) == 0, "能力唯一性索引表容量必须是 2 的幂");
    };

    /**
     *  @brief 构建证据集合
     *  @tparam Shape 组装输入形状
     *  @note 成员名是 string_view：只用于常量求值期比对，不进入运行期计划。
     *        count 必须由生成器填写真实条数，算法先做界限检查再索引。
     */
    template <typename Shape>
    struct AssemblyEvidence
    {
        static inline constexpr size_t kSelectedSlots =
            Shape::kSelectedEvidenceCapacity > 0 ? Shape::kSelectedEvidenceCapacity : 1;
        static inline constexpr size_t kBindingSlots =
            Shape::kBindingEvidenceCapacity > 0 ? Shape::kBindingEvidenceCapacity : 1;
        static inline constexpr size_t kEdgeSlots =
            Shape::kEdgeEvidenceCapacity > 0 ? Shape::kEdgeEvidenceCapacity : 1;
        static inline constexpr size_t kSlotSlots =
            Shape::kSlotEvidenceCapacity > 0 ? Shape::kSlotEvidenceCapacity : 1;
        static inline constexpr size_t kRuntimeSlots =
            Shape::kRuntimeEvidenceCapacity > 0 ? Shape::kRuntimeEvidenceCapacity : 1;

        size_t selected_count = 0;
        size_t binding_count  = 0;
        size_t edge_count     = 0;
        size_t slot_count     = 0;
        size_t runtime_count  = 0;

        SelectedEvidence       selected[kSelectedSlots] = {};
        BindingEvidence        bindings[kBindingSlots]  = {};
        EdgeEvidence           edges[kEdgeSlots]        = {};
        SlotEvidence           slots[kSlotSlots]        = {};
        RuntimeBindingEvidence runtime[kRuntimeSlots]   = {};
    };

    /**
     *  @brief 第三个操作族的完整输入（计划书 5.4）
     *  @tparam Shape 容量形状
     *  @note 四个成员都是显式数据：结构修订、完整 catalog、本产品选中身份、构建证据。
     *        schema_revision 必须等于当前头文件的 kSchemaRevision，否则在声明阶段直接拒绝，
     *        避免旧生成物悄悄喂进新语义。
     */
    template <typename Shape>
    struct AssemblyInput
    {
        uint32_t                                           schema_revision = kSchemaRevision;   // 必须等于当前头文件修订
        typename Shape::CatalogTypeAlias                   catalog         = {};               // 全部已知插件声明及能力元数据
        std::array<PluginIdentity, Shape::kActiveCapacity> selected        = {};               // 本产品显式选中的插件身份
        AssemblyEvidence<Shape>                            evidence        = {};               // 构建系统派生的证据
    };

    /**
     *  @brief 结构化诊断
     *  @note 名字不复制：调用方用 plugin/other 下标映射回自己的 catalog/active 数据取名字。
     *        plugin 的含义随阶段变化：Declaration/CatalogBinding 阶段是 catalog 下标，
     *        Selection 及以后是 canonical active 下标；other 是同一个阶段里的对端下标。
     */
    struct AssemblyDiagnostic
    {
        PluginError   code           = PluginError::None;
        AssemblyPhase phase          = AssemblyPhase::None;
        uint16_t      plugin         = kNoIndex;      // 出错的下游/插件
        uint16_t      other          = kNoIndex;      // 对端插件（重复、冲突、碰撞、证据目标）
        uint16_t      requirement    = 0;             // 需求组下标
        uint16_t      candidate      = kNoIndex;      // 具体候选/provides 记录下标；MissingProvider 为 kNoIndex，Ambiguous 为第一个命中候选
        uint16_t      evidence_index = 0;             // 证据记录下标
    };

    /**
     *  @brief 编译期组装结果（计划书 5.4）
     *  @tparam Shape 容量形状
     *  @note 只含身份、索引、槽位归属、接口修订与诊断；不携带任何指向常量求值局部对象的指针，
     *        也不含动态容器。canonical active 下标按精确插件名的名字字节序编号，
     *        因此结果与 catalog 顺序、选中输入顺序、候选顺序都无关。
     */
    template <typename Shape>
    struct AssemblyResult
    {
        static inline constexpr size_t kDeclaredActiveCapacity = Shape::kActiveCapacity;
        static inline constexpr size_t kActiveSlots = kDeclaredActiveCapacity > kMaxActive
                                                        ? kMaxActive
                                                        : (kDeclaredActiveCapacity > 0 ? kDeclaredActiveCapacity : 1);
        static inline constexpr size_t kEdgeSlots        = kActiveSlots * kMaxRequirements;
        static inline constexpr size_t kBindingSlots     = kActiveSlots * kMaxRequirements;
        static inline constexpr size_t kProvisionSlots   = Shape::kProvisionSlots;

        uint16_t active_count     = 0;                      // 规范化后的选中插件数
        uint16_t catalog_count    = 0;                      // 输入 catalog 的条目数
        uint16_t edge_count       = 0;                      // 去重后的真实依赖边数
        uint16_t provision_count  = 0;                      // 选中插件的 provides 总数（含无槽位能力）
        uint32_t schema_revision  = kSchemaRevision;        // 输入结构修订
        uint64_t fingerprint      = 0;                      // 确定性指纹
        bool     error_is_cycle   = false;                  // error_nodes 是否是真实闭合环
        uint16_t error_node_count = 0;                      // 环成员个数

        AssemblyDiagnostic diagnostic{};
        uint16_t           error_nodes[kActiveSlots] = {};                    // 真实闭合环路径（下游->上游）
        uint16_t           candidate_match_count = 0;                         // Missing/Ambiguous 相关候选命中数（Missing 为 0）
        uint16_t           candidate_match[kMaxCandidates] = {};              // 命中候选在声明候选表中的绝对下标
        uint16_t           candidate_match_active[kMaxCandidates] = {};       // 命中候选对应的 canonical active 下标

        PluginIdentity active_identity[kActiveSlots]          = {};           // canonical active 身份（按名字排序）
        uint16_t       active_catalog_index[kActiveSlots]     = {};           // canonical -> catalog
        uint16_t       active_requirement_count[kActiveSlots] = {};           // 每插件实际需求组数
        uint16_t       active_provision_count[kActiveSlots]   = {};           // 每插件声明的 provides 数
        uint16_t       topo_order[kActiveSlots]               = {};           // canonical 拓扑序
        uint16_t       provider[kBindingSlots]                = {};           // (active, req) -> provider
        uint16_t       edge_begin[kActiveSlots + 1]           = {};           // 每个下游的边区间起点
        uint16_t       edge_target[kEdgeSlots]                = {};           // 上游 canonical 下标
        uint16_t       slot_owner[kMaxSlots]                  = {};           // 槽位 -> active owner，kNoIndex 为空

        // 选中插件的 provides 扁平表：按 (canonical active, capability 名字字节序) 规范化排序。
        // 每个 provision 都记录自己的 owner/修订/槽位，不再用 active_slot/active_api_revision 代表整个插件。
        uint16_t provision_owner[kProvisionSlots]              = {};          // canonical active owner
        uint16_t provision_begin[kActiveSlots + 1]             = {};          // owner -> 其 provides 区间
        int16_t  provision_slot[kProvisionSlots]               = {};          // kNoSlot 表示纯头文件能力
        uint32_t provision_api_revision[kProvisionSlots]       = {};          // 该能力自己的接口修订
        uint32_t provision_capability_length[kProvisionSlots]  = {};          // 能力名长度
        char     provision_capability[kProvisionSlots][kNameCapacity] = {};   // 能力名拥有存储

        /**
         *  @brief 按精确插件名查找 canonical active 下标
         *  @param name 插件名
         *  @return 找到返回下标，否则 kNoIndex
         *  @note active_identity 已按精确名字字节序规范化排序，因此走二分查找（log2(128) 次比较），
         *        不再对每次调用做 128 项线性名字扫描；相等判定仍是完整名字比较。
         */
        constexpr uint16_t find_active(std::string_view name) const noexcept
        {
            uint16_t low  = 0;
            uint16_t high = active_count;
            while (low < high)
            {
                const uint16_t middle = static_cast<uint16_t>((low + high) >> 1);
                if (detail::identity_less_than_view(active_identity[middle], name))
                {
                    low = static_cast<uint16_t>(middle + 1);
                }
                else
                {
                    high = middle;
                }
            }
            if (low < active_count && active_identity[low].view() == name)
            {
                return low;
            }
            return kNoIndex;
        }

        /**
         *  @brief 第 index 条 provision 的能力名视图
         *  @param index provision 下标，必须小于 provision_count
         *  @return 指向本结果拥有存储的临时视图；复制结果后需重新取得
         */
        constexpr std::string_view provision_capability_view(size_t index) const noexcept
        {
            return { provision_capability[index], provision_capability_length[index] };
        }
    };

    namespace detail
    {
        // catalog 身份索引的开放寻址表容量：2 倍 catalog 上限，取 2 的幂以便用掩码取模
        inline constexpr size_t   kCatalogSlotCount = 2 * kMaxCatalog;
        inline constexpr uint16_t kCatalogSlotMask  = static_cast<uint16_t>(kCatalogSlotCount - 1);
        static_assert((kCatalogSlotCount & (kCatalogSlotCount - 1)) == 0, "catalog 槽表容量必须是 2 的幂");

        // 声明组内暂态索引的槽数：2 倍候选上限，保证 16 个候选最多 32 次探测内必定找到空槽
        inline constexpr size_t   kGroupSlotCount = 2 * kMaxCandidates;
        inline constexpr uint16_t kGroupSlotMask  = static_cast<uint16_t>(kGroupSlotCount - 1);
        static_assert((kGroupSlotCount & (kGroupSlotCount - 1)) == 0, "组内暂态槽表容量必须是 2 的幂");
        static_assert(kMaxCandidates < kGroupSlotCount, "组内暂态槽表必须比候选上限更稀疏，否则探测可能无空槽");

        template <typename Group>
        struct group_size;

        template <typename Element, size_t Count>
        struct group_size<std::array<Element, Count>>
        {
            static inline constexpr size_t value = Count;
        };

        template <size_t GroupCount, size_t CandidateCount>
        constexpr void set_declaration_error(PluginDeclaration<GroupCount, CandidateCount>& declaration,
                                             PluginError code, size_t group, size_t candidate) noexcept
        {
            if (declaration.error == PluginError::None)
            {
                declaration.error           = code;
                declaration.error_group     = static_cast<uint16_t>(group);
                declaration.error_candidate = static_cast<uint16_t>(candidate);
            }
        }

        /**
         *  @brief 追加一个候选：校验格式、算标量键、把名字字节写进拥有池
         *  @note 组内重复在这里不做前缀扫描：sort_and_validate 的声明阶段用 32 槽暂态表按标量键单趟判定
         *        （满名确认后才算重复），诊断类别是 DuplicateCandidate，位置先于契约逐候选匹配。
         */
        template <size_t GroupCount, size_t CandidateCount>
        constexpr void add_candidate(PluginDeclaration<GroupCount, CandidateCount>& declaration,
                                     std::string_view name, size_t group_index) noexcept
        {
            if (declaration.candidate_count >= CandidateCount)
            {
                set_declaration_error(declaration, PluginError::InvalidDeclaration, group_index,
                                      declaration.candidate_count);
                return;
            }
            const size_t index  = declaration.candidate_count;
            const size_t length = name.size();
            if (name.data() == nullptr || length == 0 || length > kMaxNameBytes || !is_name_head(name[0]))
            {
                set_declaration_error(declaration, PluginError::InvalidName, group_index, index);
                return;
            }
            // 首字符集合是尾字符集合的子集，循环内只保留一次查表判定，不再重复测试 i != 0
            uint64_t hash     = kFnvOffsetBasis;
            uint64_t prefix0  = 0;
            uint64_t prefix1  = 0;
            bool     is_valid = true;
            const uint32_t pool_offset = declaration.candidate_offset[index];
            for (size_t i = 0; i < length; ++i)
            {
                const char character = name[i];
                if (!is_name_tail(character))
                {
                    is_valid = false;
                    break;
                }
                const unsigned char byte = static_cast<unsigned char>(character);
                declaration.candidate_pool[pool_offset + i] = character;
                hash ^= byte;
                hash *= kFnvPrime;
                if (i < 8)
                {
                    prefix0 |= static_cast<uint64_t>(byte) << (8 * i);
                }
                else if (i < 16)
                {
                    prefix1 |= static_cast<uint64_t>(byte) << (8 * (i - 8));
                }
            }
            if (!is_valid)
            {
                set_declaration_error(declaration, PluginError::InvalidName, group_index, index);
                return;
            }
            declaration.candidate_length[index]      = static_cast<uint16_t>(length);
            declaration.candidate_offset[index + 1]  = declaration.candidate_offset[index] + static_cast<uint32_t>(length);
            declaration.candidate_id[index]          = hash;
            declaration.candidate_prefix0[index]     = prefix0;
            declaration.candidate_prefix1[index]     = prefix1;
            if (name_equals(declaration.identity, declaration.candidate_pool + declaration.candidate_offset[index],
                            length, hash, prefix0, prefix1))
            {
                // 完整名字相等才算自依赖；单纯哈希/前缀相同是碰撞，留给 catalog 阶段诊断
                set_declaration_error(declaration, PluginError::SelfDependency, group_index, index);
            }
            ++declaration.candidate_count;
        }

        /**
         *  @brief 追加一个需求组（外层 AND 的一项）
         */
        template <size_t GroupCount, size_t CandidateCount, typename Group>
        constexpr void add_group(PluginDeclaration<GroupCount, CandidateCount>& declaration,
                                 const Group& group) noexcept
        {
            const size_t group_index             = declaration.group_count;
            declaration.group_begin[group_index] = declaration.candidate_count;
            if (group_index >= kMaxRequirements)
            {
                // 结构上限：第 33 组开始只记录诊断，不再展开，避免把越界组写进结果索引
                set_declaration_error(declaration, PluginError::TooManyRequirements, group_index, 0);
                ++declaration.group_count;
                declaration.group_begin[declaration.group_count] = declaration.candidate_count;
                return;
            }
            if (group.size() == 0)
            {
                set_declaration_error(declaration, PluginError::EmptyRequirementGroup, group_index, 0);
            }
            if (group.size() > kMaxCandidates)
            {
                set_declaration_error(declaration, PluginError::TooManyCandidates, group_index, 0);
            }
            for (std::string_view name : group)
            {
                add_candidate(declaration, name, group_index);
            }
            ++declaration.group_count;
            declaration.group_begin[declaration.group_count] = declaration.candidate_count;
        }

        template <size_t GroupCount, size_t CandidateCount, typename Tuple, size_t... Index>
        constexpr void add_tuple_groups(PluginDeclaration<GroupCount, CandidateCount>& declaration,
                                        const Tuple& groups, std::index_sequence<Index...>) noexcept
        {
            (add_group(declaration, std::get<Index>(groups)), ...);
        }

        /**
         *  @brief 第二个操作族的 tuple 形态实现：外层 tuple = AND，每个元素是一组候选（OR）
         */
        template <typename Identity, typename... Groups>
        constexpr auto declare_tuple(Identity identity, const std::tuple<Groups...>& groups) noexcept
            -> PluginDeclaration<sizeof...(Groups), (group_size<Groups>::value + ... + 0)>
        {
            constexpr size_t kGroups     = sizeof...(Groups);
            constexpr size_t kCandidates = (group_size<Groups>::value + ... + 0);

            PluginDeclaration<kGroups, kCandidates> declaration{};
            declaration.identity = identity;
            if (!identity.is_valid)
            {
                set_declaration_error(declaration, PluginError::InvalidName, 0, 0);
                return declaration;
            }
            add_tuple_groups(declaration, groups, std::index_sequence_for<Groups...>{});
            return declaration;
        }

        /**
         *  @brief 第二个操作族的矩形形态实现：G 个需求组，每组恰好 K 个候选
         */
        template <typename Identity, typename Element, size_t Candidates, size_t Groups>
        constexpr auto declare_rect(Identity identity,
                                    const std::array<std::array<Element, Candidates>, Groups>& groups) noexcept
            -> PluginDeclaration<Groups, Groups * Candidates>
        {
            PluginDeclaration<Groups, Groups * Candidates> declaration{};
            declaration.identity = identity;
            if (!identity.is_valid)
            {
                set_declaration_error(declaration, PluginError::InvalidName, 0, 0);
                return declaration;
            }
            for (size_t group_index = 0; group_index < Groups; ++group_index)
            {
                add_group(declaration, groups[group_index]);
            }
            return declaration;
        }

        /**
         *  @brief 扁平能力表的一项：某个 catalog 条目提供的一个能力（名字视图指向输入数据）
         */
        struct CapabilityRef
        {
            std::string_view name         = {};
            uint32_t         api_revision = 0;
            int16_t          slot         = kNoSlot;
            uint16_t         owner        = kNoIndex;   // 所属 catalog 下标
        };

        // 计划状态：排序/校验期间的可变工作区，全部定长数组，不分配堆内存，
        // 也不把任何指针写进返回给调用方的结果。
        template <typename Shape>
        struct PlanState
        {
            using Result = AssemblyResult<Shape>;

            // 结果由 sort_and_validate 持有，计划状态只写它，不再持有自己的一份：
            // 结果因此只构造一次、只返回一次（NRVO），返回路径上不做整份定长结果的复制。
            Result&        result;

            uint16_t       catalog_count       = 0;
            // catalog 名字只保存指向输入 catalog 的视图（常量求值期内有效，绝不进入返回结果），
            // 不再为每个条目整体复制 106 字节的 PluginIdentity：满规模下这曾是声明阶段的主要开销之一。
            std::string_view catalog_name[kMaxCatalog]        = {};
            std::string_view catalog_target[kMaxCatalog]     = {};
            std::string_view catalog_namespace[kMaxCatalog]   = {};   // 精确 ASCII 命名空间，用于跨 catalog 冲突诊断
            uint64_t       catalog_namespace_hash[kMaxCatalog] = {};  // 命名空间的 FNV-1a 64（冲突表的键）
            uint64_t       catalog_key_id[kMaxCatalog]       = {};   // 身份 FNV-1a 64（身份查找的键）
            // 候选查找的标量确认键：命中同 ID 后先比长度与两个前缀字，标量全同才做满名确认，
            // 因此常规 13 字节名字根本不进入逐字节比较（常量求值里逐字节比较最贵）。
            uint32_t       catalog_key_length[kMaxCatalog]   = {};
            uint64_t       catalog_key_prefix0[kMaxCatalog]  = {};
            uint64_t       catalog_key_prefix1[kMaxCatalog]  = {};
            uint16_t       catalog_slot[kCatalogSlotCount]   = {};   // 身份开放寻址表（存的是下标 + 1）
            uint16_t       catalog_macro_slot[kCatalogSlotCount] = {};   // 身份宏开放寻址表
            uint16_t       catalog_namespace_slot[kCatalogSlotCount] = {}; // 命名空间开放寻址表
            uint16_t       active_of_catalog[kMaxCatalog]    = {};   // 未选中为 kNoIndex

            uint16_t       capability_begin[kMaxCatalog]     = {};   // 该 catalog 条目的能力在扁平表的起点
            uint16_t       capability_count[kMaxCatalog]     = {};   // 该 catalog 条目的能力数
            uint16_t       capability_total                  = 0;
            CapabilityRef  capability[Shape::kProvisionSlots] = {};  // 全 catalog provides 容量由 Shape 推导
            // 产品级能力唯一性表：把 O(provides²) 的名字两两比较换成一次哈希 + 有界探测
            uint16_t       capability_slot[Shape::kCapabilitySlotCount] = {};

            uint16_t       in_degree[kMaxActive]             = {};
            uint16_t       forward_begin[kMaxActive + 1]     = {};
            uint16_t       forward_target[kMaxActive * kMaxRequirements] = {};
            bool           is_sorted[kMaxActive]             = {};

            // 该 catalog 条目的修订记录是否恰好按候选顺序排列（声明阶段逐项验证过）：
            // 绑定阶段据此直接按下标取修订区间，不再对每组做一次名字扫描。
            bool           revision_in_order[kMaxCatalog]    = {};
        };

        template <typename Shape>
        constexpr bool has_error(const PlanState<Shape>& state) noexcept
        {
            return state.result.diagnostic.code != PluginError::None;
        }

        template <typename Shape>
        constexpr void fail(PlanState<Shape>& state, AssemblyPhase phase, PluginError code,
                            uint16_t plugin, uint16_t requirement, uint16_t candidate, uint16_t other) noexcept
        {
            AssemblyDiagnostic& diagnostic = state.result.diagnostic;
            if (diagnostic.code != PluginError::None)
            {
                return;
            }
            diagnostic.code        = code;
            diagnostic.phase       = phase;
            diagnostic.plugin      = plugin;
            diagnostic.other       = other;
            diagnostic.requirement = requirement;
            diagnostic.candidate   = candidate;
        }

        // ---------- 阶段 1：声明 / 身份 / 契约 ----------

        /**
         *  @brief 声明组内校验：容量、候选格式、自依赖、组内重复候选（先于契约逐候选匹配）
         *  @param state 计划状态
         *  @param declaration 声明（拥有存储的候选表）
         *  @param index 所属 catalog 下标，仅用于诊断
         *  @param group 组下标，必须小于 declaration.group_count
         *  @return 全部通过返回 true；任何一项失败都已写入诊断并返回 false
         *  @note 顺序固定为：容量（区间/上限）-> 逐候选格式与池边界 -> 自依赖 -> 组内重复。
         *        组内重复用 32 槽暂态表按候选标量键 (长度, ID, 前缀字) 单趟判定，命中后**逐字节确认完整名字**，
         *        因此“同 ID 不同名”不会被判成重复候选；这类哈希碰撞仍留给 catalog 阶段的 NameHashCollision。
         *        槽表容量是候选上限的 2 倍，探测次数有界，不会因为恶意同哈希输入无限循环。
         */
        template <typename Shape, typename Declaration>
        constexpr bool validate_group_declaration(PlanState<Shape>& state, const Declaration& declaration,
                                                  uint16_t index, uint16_t group) noexcept
        {
            const uint16_t begin = declaration.group_begin[group];
            const uint16_t end   = declaration.group_begin[group + 1];
            if (begin > end || end > declaration.candidate_count)
            {
                // 组区间被写坏：先拒绝，避免把越界下标带进候选数组
                fail(state, AssemblyPhase::Declaration, PluginError::InvalidDeclaration, index, group, 0, kNoIndex);
                return false;
            }
            const uint16_t count = static_cast<uint16_t>(end - begin);
            if (count == 0)
            {
                fail(state, AssemblyPhase::Declaration, PluginError::EmptyRequirementGroup, index, group, 0, kNoIndex);
                return false;
            }
            if (count > kMaxCandidates)
            {
                fail(state, AssemblyPhase::Declaration, PluginError::TooManyCandidates, index, group, 0, kNoIndex);
                return false;
            }

            // 单候选组不可能出现组内重复：跳过 32 槽暂态表的零初始化与探测，
            // 只保留候选格式、池边界与自依赖三项检查，诊断类别与优先级完全不变。
            if (count == 1)
            {
                const uint32_t length      = declaration.candidate_length[begin];
                const uint32_t pool_offset = declaration.candidate_offset[begin];
                if (length == 0 || length > kMaxNameBytes)
                {
                    fail(state, AssemblyPhase::Declaration, PluginError::InvalidName, index, group, begin, kNoIndex);
                    return false;
                }
                if (pool_offset > Declaration::kPoolBytes || length > Declaration::kPoolBytes - pool_offset)
                {
                    fail(state, AssemblyPhase::Declaration, PluginError::InvalidDeclaration, index, group, begin,
                         kNoIndex);
                    return false;
                }
                // candidate_offset 必须与 candidate_length 自洽：offset[i+1] == offset[i] + length[i]。
                // 这条同时给出单调性（长度恒 > 0），畸形 POD 的回退/重叠偏移因此在下标式读取池之前就被拒绝。
                if (declaration.candidate_offset[begin + 1] != pool_offset + length)
                {
                    fail(state, AssemblyPhase::Declaration, PluginError::InvalidDeclaration, index, group, begin,
                         kNoIndex);
                    return false;
                }
                if (name_equals(declaration.identity, declaration.candidate_pool + pool_offset, length,
                                declaration.candidate_id[begin], declaration.candidate_prefix0[begin],
                                declaration.candidate_prefix1[begin]))
                {
                    fail(state, AssemblyPhase::Declaration, PluginError::SelfDependency, index, group, begin, index);
                    return false;
                }
                return true;
            }

            uint16_t slots[kGroupSlotCount] = {};       // 值 = 组内候选下标 + 1，0 表示空槽
            for (uint16_t offset = 0; offset < count; ++offset)
            {
                const uint16_t candidate   = static_cast<uint16_t>(begin + offset);
                const uint32_t length      = declaration.candidate_length[candidate];
                const uint32_t pool_offset = declaration.candidate_offset[candidate];
                if (length == 0 || length > kMaxNameBytes)
                {
                    fail(state, AssemblyPhase::Declaration, PluginError::InvalidName, index, group, candidate, kNoIndex);
                    return false;
                }
                if (pool_offset > Declaration::kPoolBytes || length > Declaration::kPoolBytes - pool_offset)
                {
                    // 名字存不进本条目声明的字节池：容量被写坏，不是普通格式错误
                    fail(state, AssemblyPhase::Declaration, PluginError::InvalidDeclaration, index, group, candidate,
                         kNoIndex);
                    return false;
                }
                // candidate_offset 单调且与 candidate_length 自洽（offset[i+1] == offset[i] + length[i]）
                if (declaration.candidate_offset[candidate + 1] != pool_offset + length)
                {
                    fail(state, AssemblyPhase::Declaration, PluginError::InvalidDeclaration, index, group, candidate,
                         kNoIndex);
                    return false;
                }
                const char*    bytes   = declaration.candidate_pool + pool_offset;
                const uint64_t id      = declaration.candidate_id[candidate];
                const uint64_t prefix0 = declaration.candidate_prefix0[candidate];
                const uint64_t prefix1 = declaration.candidate_prefix1[candidate];
                if (name_equals(declaration.identity, bytes, length, id, prefix0, prefix1))
                {
                    fail(state, AssemblyPhase::Declaration, PluginError::SelfDependency, index, group, candidate, index);
                    return false;
                }
                uint16_t slot = static_cast<uint16_t>(id & kGroupSlotMask);
                for (uint16_t probe = 0; probe < kGroupSlotCount; ++probe)
                {
                    const uint16_t occupied = slots[slot];
                    if (occupied == 0)
                    {
                        slots[slot] = static_cast<uint16_t>(candidate + 1);
                        break;
                    }
                    const uint16_t previous = static_cast<uint16_t>(occupied - 1);
                    if (declaration.candidate_length[previous] == length &&
                        declaration.candidate_id[previous] == id &&
                        declaration.candidate_prefix0[previous] == prefix0 &&
                        declaration.candidate_prefix1[previous] == prefix1)
                    {
                        const char* other = declaration.candidate_pool + declaration.candidate_offset[previous];
                        bool        same  = true;
                        for (uint32_t i = 16; i < length; ++i)     // 前 16 字节已由标量键覆盖
                        {
                            if (other[i] != bytes[i])
                            {
                                same = false;
                                break;
                            }
                        }
                        if (same)
                        {
                            fail(state, AssemblyPhase::Declaration, PluginError::DuplicateCandidate, index, group,
                                 candidate, previous);
                            return false;
                        }
                    }
                    slot = static_cast<uint16_t>((slot + 1) & kGroupSlotMask);
                }
            }
            return true;
        }

        /**
         *  @brief 逐候选修订记录与组内候选名集合的一一对应校验
         *  @param in_order 出参：记录是否恰好按候选顺序排列（供绑定阶段按下标直接取修订区间）
         *  @return 一一对应成立返回 true；否则写入 ContractCandidateMismatch 并返回 false
         *  @note 常见情形（生成器按候选顺序写记录）只做“一条记录对一条候选”的一次名字比较；
         *        顺序被打乱时回退到无序匹配，语义仍是同一集合的双向一一对应：
         *        重复记录（两条记录抢同一个候选）与漏项（记录名不在组内）都必须被拒。
         */
        template <typename Shape, typename Metadata, typename Declaration>
        constexpr bool match_group_revisions(PlanState<Shape>& state, const Metadata& metadata,
                                            const Declaration& declaration, uint16_t index, uint16_t group,
                                            bool& in_order) noexcept
        {
            const RequirementContract& contract = metadata.contracts[group];
            const uint16_t             begin    = declaration.group_begin[group];
            const uint16_t             count    = contract.revision_count;
            const uint16_t             base     = contract.revision_begin;

            bool ordered = true;
            for (uint16_t record_index = 0; record_index < count; ++record_index)
            {
                const CandidateRevision& record = metadata.revisions[base + record_index];
                // 先判对应关系：记录名与同位置候选名完全相同时，名字合法性已由 validate_group_declaration
                // 在该候选上验证过，这里不必再扫一遍整串字节；只有对不上位置时才补做格式校验，
                // 因此 InvalidName 仍先于 ContractCandidateMismatch 报告。
                if (declaration.candidate_view(begin + record_index) != record.name)
                {
                    if (!is_valid_name(record.name))
                    {
                        fail(state, AssemblyPhase::Declaration, PluginError::InvalidName, index, group, record_index,
                             kNoIndex);
                        return false;
                    }
                    ordered = false;
                }
                if (record.min_revision == 0 || record.min_revision > record.max_revision)
                {
                    fail(state, AssemblyPhase::Declaration, PluginError::InvalidRevisionRange, index, group,
                         record_index, kNoIndex);
                    return false;
                }
            }
            if (ordered)
            {
                // 本组按序：不改 in_order（后面任意一组乱序会把整个条目降级为名字扫描）
                return true;
            }

            bool consumed[kMaxCandidates] = {};
            for (uint16_t record_index = 0; record_index < count; ++record_index)
            {
                const CandidateRevision& record  = metadata.revisions[base + record_index];
                uint16_t                 matched = kNoIndex;
                for (uint16_t candidate = 0; candidate < count; ++candidate)
                {
                    if (!consumed[candidate] && declaration.candidate_view(begin + candidate) == record.name)
                    {
                        matched = candidate;
                        break;
                    }
                }
                if (matched == kNoIndex)
                {
                    // 记录名不在组内，或同一个候选被两条记录重复占用：都不是一一对应
                    fail(state, AssemblyPhase::Declaration, PluginError::ContractCandidateMismatch, index, group,
                         record_index, kNoIndex);
                    return false;
                }
                consumed[matched] = true;
            }
            in_order = false;
            return true;
        }

        /**
         *  @brief 收集一个 catalog 条目：校验格式、容量、能力与契约，并把能力写进扁平表
         *  @note 只做与**本条目自身数据**有关的检查；跨条目比较在 build_identity_index 与后续阶段。
         */
        template <typename Shape, typename Entry>
        constexpr void collect_entry(PlanState<Shape>& state, const Entry& entry, uint16_t index) noexcept
        {
            if (has_error(state))
            {
                return;
            }
            if (index >= kMaxCatalog)
            {
                fail(state, AssemblyPhase::Declaration, PluginError::TooManyCatalog, kNoIndex, 0, 0, kNoIndex);
                return;
            }

            using Declaration = typename Entry::DeclarationType;
            using Metadata    = typename Entry::MetadataType;

            const Declaration& declaration      = entry.declaration;
            const Metadata&    metadata         = entry.metadata;
            state.catalog_name[index]           = declaration.identity.view();
            state.catalog_key_id[index]         = declaration.identity.id;
            state.catalog_key_length[index]     = declaration.identity.length;
            state.catalog_key_prefix0[index]    = declaration.identity.prefix0;
            state.catalog_key_prefix1[index]    = declaration.identity.prefix1;
            state.catalog_target[index]         = metadata.target;
            state.catalog_namespace[index]      = metadata.cpp_namespace;
            state.catalog_namespace_hash[index] = hash_bytes(kFnvOffsetBasis, metadata.cpp_namespace);

            if (!declaration.identity.is_valid)
            {
                fail(state, AssemblyPhase::Declaration, PluginError::InvalidName, index, 0, 0, kNoIndex);
                return;
            }
            if (declaration.error != PluginError::None)
            {
                fail(state, AssemblyPhase::Declaration, declaration.error, index, declaration.error_group,
                     declaration.error_candidate, kNoIndex);
                return;
            }
            if (declaration.group_count > Declaration::kGroupCapacity || declaration.group_count > kMaxRequirements)
            {
                // 组数上限有两个来源：本条目声明的形状容量与全局 32。两者都必须在任何 group_begin /
                // contracts 索引之前判定，否则手工构造的畸形 POD 会把越界组下标带进数组。
                // 正常声明在第 33 组时由 add_group 记 TooManyRequirements，早已在上面返回；
                // 走到这里的都是容量被写坏的 POD，因此归为 InvalidDeclaration。
                fail(state, AssemblyPhase::Declaration, PluginError::InvalidDeclaration, index, 0, 0, kNoIndex);
                return;
            }
            if (declaration.candidate_count > Declaration::kCandidateCapacity)
            {
                fail(state, AssemblyPhase::Declaration, PluginError::InvalidDeclaration, index, 0, 0, kNoIndex);
                return;
            }
            if (!is_valid_target(metadata.target))
            {
                fail(state, AssemblyPhase::Declaration, PluginError::InvalidTargetName, index, 0, 0, kNoIndex);
                return;
            }
            if (!is_valid_namespace(metadata.cpp_namespace))
            {
                fail(state, AssemblyPhase::Declaration, PluginError::InvalidNamespace, index, 0, 0, kNoIndex);
                return;
            }
            if (metadata.version == 0)
            {
                fail(state, AssemblyPhase::Declaration, PluginError::InvalidDeclaration, index, 0, 0, kNoIndex);
                return;
            }
            if (static_cast<uint8_t>(metadata.kind) > static_cast<uint8_t>(PluginKind::Interface) ||
                static_cast<uint8_t>(metadata.lifecycle) > static_cast<uint8_t>(PluginLifecycle::None))
            {
                // 枚举值本身不在合法集合内：不能静默当成 Module/Standard 继续组装，先按格式拒绝
                fail(state, AssemblyPhase::Declaration, PluginError::InvalidDeclaration, index, 0, 0, kNoIndex);
                return;
            }
            if (metadata.kind == PluginKind::Interface && metadata.lifecycle != PluginLifecycle::None)
            {
                fail(state, AssemblyPhase::Declaration, PluginError::InterfaceLifecycleMismatch, index, 0, 0, kNoIndex);
                return;
            }
            if (metadata.provides_count > Metadata::kProvidesCapacity)
            {
                // 容量来自该 catalog 条目类型的 P，不是全局 4；超出必须结构化拒绝，不静默截断
                fail(state, AssemblyPhase::Declaration, PluginError::TooManyProvides, index, 0, 0, kNoIndex);
                return;
            }

            // ---- provides：能力名、接口修订、槽位、能力唯一 ----
            size_t slot_count = 0;
            for (uint16_t provide = 0; provide < metadata.provides_count; ++provide)
            {
                const Provision& provision = metadata.provides[provide];
                if (!is_valid_name(provision.capability))
                {
                    fail(state, AssemblyPhase::Declaration, PluginError::InvalidCapability, index, 0, provide, kNoIndex);
                    return;
                }
                if (provision.api_revision == 0)
                {
                    // 插件发布版本不能冒充接口修订：接口修订必须显式声明且 >= 1
                    fail(state, AssemblyPhase::Declaration, PluginError::InvalidRevision, index, 0, provide, kNoIndex);
                    return;
                }
                if (provision.slot < kNoSlot || provision.slot >= static_cast<int16_t>(kMaxSlots))
                {
                    fail(state, AssemblyPhase::Declaration, PluginError::InvalidSlot, index, 0, provide, kNoIndex);
                    return;
                }
                if (provision.slot != kNoSlot)
                {
                    ++slot_count;
                }
                for (uint16_t previous = 0; previous < provide; ++previous)
                {
                    if (metadata.provides[previous].capability == provision.capability)
                    {
                        fail(state, AssemblyPhase::Declaration, PluginError::DuplicateCapability, index, 0,
                             provide, kNoIndex);
                        return;
                    }
                }
            }
            if (metadata.kind == PluginKind::Interface && slot_count != 0)
            {
                // 计划书 12.2：interface 首版 lifecycle=none 且没有运行期槽位；
                // module 的 lifecycle=none + slot 不在 P1 额外冻结，交由 P3 runtime 契约处理。
                fail(state, AssemblyPhase::Declaration, PluginError::InterfaceLifecycleMismatch, index, 0, 0, kNoIndex);
                return;
            }

            // ---- 契约：条数与需求组一一对应，逐候选修订与候选名集合一一对应 ----
            if (metadata.contract_count > Metadata::kContractSlots)
            {
                // 契约数组容量被写坏：必须先于 contracts[group] 索引拒绝
                fail(state, AssemblyPhase::Declaration, PluginError::InvalidDeclaration, index, 0, 0, kNoIndex);
                return;
            }
            if (metadata.contract_count != declaration.group_count)
            {
                fail(state, AssemblyPhase::Declaration, PluginError::ContractRequirementMismatch, index, 0, 0, kNoIndex);
                return;
            }
            size_t running  = 0;
            bool   in_order = true;
            for (uint16_t group = 0; group < metadata.contract_count; ++group)
            {
                const RequirementContract& contract = metadata.contracts[group];
                if (!is_valid_name(contract.capability))
                {
                    fail(state, AssemblyPhase::Declaration, PluginError::InvalidCapability, index, group, 0, kNoIndex);
                    return;
                }
                // 组内重复/自依赖/格式/容量必须先于契约逐候选匹配：否则两条同名候选会让
                // “逐候选修订一一对应”先失败，DuplicateCandidate 永远不可达。
                if (!validate_group_declaration(state, declaration, index, group))
                {
                    return;
                }
                const uint16_t group_candidates =
                    static_cast<uint16_t>(declaration.group_begin[group + 1] - declaration.group_begin[group]);
                if (contract.revision_begin != running || contract.revision_count != group_candidates)
                {
                    fail(state, AssemblyPhase::Declaration, PluginError::ContractCandidateMismatch, index, group, 0, kNoIndex);
                    return;
                }
                if (static_cast<size_t>(contract.revision_begin) + contract.revision_count > Metadata::kRevisionSlots)
                {
                    // 修订区间必须先落在本条目声明的修订容量内，再索引 metadata.revisions
                    fail(state, AssemblyPhase::Declaration, PluginError::InvalidDeclaration, index, group, 0, kNoIndex);
                    return;
                }
                if (!match_group_revisions(state, metadata, declaration, index, group, in_order))
                {
                    return;
                }
                running += group_candidates;
            }
            if (running != declaration.candidate_count)
            {
                fail(state, AssemblyPhase::Declaration, PluginError::ContractCandidateMismatch, index, 0, 0, kNoIndex);
                return;
            }
            // 记录顺序与候选顺序一致时，绑定阶段按下标直取修订区间，不必再做逐组名字扫描
            state.revision_in_order[index] = in_order;

            // ---- 能力扁平表（供候选能力校验、能力唯一性与指纹使用） ----
            state.capability_begin[index] = state.capability_total;
            state.capability_count[index] = metadata.provides_count;
            for (uint16_t provide = 0; provide < metadata.provides_count; ++provide)
            {
                if (state.capability_total >= Shape::kProvisionSlots) [[unlikely]]
                {
                    fail(state, AssemblyPhase::Declaration, PluginError::InvalidDeclaration, index, 0, provide, kNoIndex);
                    return;
                }
                CapabilityRef& ref = state.capability[state.capability_total++];
                ref.name           = metadata.provides[provide].capability;
                ref.api_revision   = metadata.provides[provide].api_revision;
                ref.slot           = metadata.provides[provide].slot;
                ref.owner          = index;
            }
        }

        template <typename Shape, typename Catalog, size_t... Index>
        constexpr void collect_catalog(PlanState<Shape>& state, const Catalog& catalog,
                                       std::index_sequence<Index...>) noexcept
        {
            uint16_t cursor = 0;
            ((collect_entry(state, std::get<Index>(catalog), cursor++)), ...);
        }

        /**
         *  @brief 阶段 1 续：重名、不同名同哈希、target 归一化后的身份宏映射冲突，以及
         *         catalog 内精确 ASCII 命名空间的一一冲突
         *  @note namespace 冲突按声明的原始字符串逐字节比较，不做宏归一化；
         *        target 的身份宏冲突仍沿用归一化口径，两者不互相替代。
         */
        template <typename Shape>
        constexpr void build_identity_index(PlanState<Shape>& state) noexcept
        {
            if (has_error(state))
            {
                return;
            }
            for (uint16_t index = 0; index < state.catalog_count; ++index)
            {
                uint16_t slot = static_cast<uint16_t>(state.catalog_key_id[index] & kCatalogSlotMask);
                for (uint16_t probe = 0; probe < kCatalogSlotCount; ++probe)
                {
                    const uint16_t occupied = state.catalog_slot[slot];
                    if (occupied == 0)
                    {
                        state.catalog_slot[slot] = static_cast<uint16_t>(index + 1);
                        break;
                    }
                    const uint16_t existing = static_cast<uint16_t>(occupied - 1);
                    if (state.catalog_key_id[existing] == state.catalog_key_id[index])
                    {
                        // 同 ID 时再比完整名字：完全同名是重名，否则是哈希碰撞，两者都不是合法身份
                        if (state.catalog_name[existing] == state.catalog_name[index])
                        {
                            fail(state, AssemblyPhase::Declaration, PluginError::DuplicatePlugin, index, 0, 0, existing);
                        }
                        else
                        {
                            fail(state, AssemblyPhase::Declaration, PluginError::NameHashCollision, index, 0, 0, existing);
                        }
                        return;
                    }
                    slot = static_cast<uint16_t>((slot + 1) & kCatalogSlotMask);
                }

                // 身份宏映射冲突：target 归一化后相同，等价于下游收到同一个 ID_USE_PLUGIN_ 宏。
                // 键由流式归一化直接算出，不再为每个条目落一个 kMacroCapacity 缓冲。
                const uint64_t macro_hash = identity_macro_hash(state.catalog_target[index]);
                uint16_t       macro_slot = static_cast<uint16_t>(macro_hash & kCatalogSlotMask);
                for (uint16_t probe = 0; probe < kCatalogSlotCount; ++probe)
                {
                    const uint16_t occupied = state.catalog_macro_slot[macro_slot];
                    if (occupied == 0)
                    {
                        state.catalog_macro_slot[macro_slot] = static_cast<uint16_t>(index + 1);
                        break;
                    }
                    const uint16_t existing = static_cast<uint16_t>(occupied - 1);
                    if (identity_macro_equal(state.catalog_target[existing], state.catalog_target[index]))
                    {
                        fail(state, AssemblyPhase::Declaration, PluginError::TargetMacroConflict, index, 0, 0, existing);
                        return;
                    }
                    macro_slot = static_cast<uint16_t>((macro_slot + 1) & kCatalogSlotMask);
                }

                // 精确 ASCII 命名空间全局唯一：不同插件复用同一个 C++ 命名空间会导致生成声明/导出头归属冲突。
                // 用命名空间哈希表代替 O(catalog²) 的名字两两比较，命中同哈希后再做完整字节确认。
                uint16_t space_slot = static_cast<uint16_t>(state.catalog_namespace_hash[index] & kCatalogSlotMask);
                for (uint16_t probe = 0; probe < kCatalogSlotCount; ++probe)
                {
                    const uint16_t occupied = state.catalog_namespace_slot[space_slot];
                    if (occupied == 0)
                    {
                        state.catalog_namespace_slot[space_slot] = static_cast<uint16_t>(index + 1);
                        break;
                    }
                    const uint16_t existing = static_cast<uint16_t>(occupied - 1);
                    if (state.catalog_namespace[existing] == state.catalog_namespace[index])
                    {
                        fail(state, AssemblyPhase::Declaration, PluginError::NamespaceConflict, index, 0, 0, existing);
                        return;
                    }
                    space_slot = static_cast<uint16_t>((space_slot + 1) & kCatalogSlotMask);
                }
            }
        }

        /**
         *  @brief 身份 -> catalog 下标：开放寻址线性探测；同 ID 但名字不同判为碰撞
         */
        template <typename Shape>
        constexpr uint16_t find_catalog_by_identity(const PlanState<Shape>& state, const PluginIdentity& identity,
                                                    bool& collision) noexcept
        {
            collision     = false;
            uint16_t slot = static_cast<uint16_t>(identity.id & kCatalogSlotMask);
            for (uint16_t probe = 0; probe < kCatalogSlotCount; ++probe)
            {
                const uint16_t occupied = state.catalog_slot[slot];
                if (occupied == 0)
                {
                    return kNoIndex;
                }
                const uint16_t catalog_index = static_cast<uint16_t>(occupied - 1);
                if (state.catalog_key_id[catalog_index] == identity.id)
                {
                    if (state.catalog_name[catalog_index] == identity.view())
                    {
                        return catalog_index;
                    }
                    collision = true;
                    return kNoIndex;
                }
                slot = static_cast<uint16_t>((slot + 1) & kCatalogSlotMask);
            }
            return kNoIndex;
        }

        /**
         *  @brief 名字 -> canonical active 下标：复用 catalog 身份开放寻址表 + active 反查缓存
         *  @param state 计划状态（catalog 索引与 active 反查必须已经建立）
         *  @param name 待查的精确插件名
         *  @return 已选中的 canonical active 下标；名字不在 catalog、或在 catalog 但未被选中、或名字非法时返回 kNoIndex
         *  @note 证据校验要对每条记录定位插件，逐条做 128 项线性名字扫描是 O(records x active)；
         *        这里只做一次 12 字节级哈希 + 有界探测，再经 active_of_catalog 一次下标取结果。
         *        命中同 ID 后仍做完整名字比较，哈希/前缀都不被当作相等。
         */
        template <typename Shape>
        constexpr uint16_t find_active_by_name(const PlanState<Shape>& state, std::string_view name) noexcept
        {
            if (name.empty() || name.size() > kMaxNameBytes)
            {
                return kNoIndex;
            }
            const uint64_t hash = hash_bytes(kFnvOffsetBasis, name);
            uint16_t       slot = static_cast<uint16_t>(hash & kCatalogSlotMask);
            for (uint16_t probe = 0; probe < kCatalogSlotCount; ++probe)
            {
                const uint16_t occupied = state.catalog_slot[slot];
                if (occupied == 0)
                {
                    return kNoIndex;
                }
                const uint16_t catalog_index = static_cast<uint16_t>(occupied - 1);
                if (state.catalog_key_id[catalog_index] == hash)
                {
                    if (state.catalog_name[catalog_index] != name)
                    {
                        return kNoIndex;        // 同 ID 不同名：请求的名字根本没有身份
                    }
                    return state.active_of_catalog[catalog_index];
                }
                slot = static_cast<uint16_t>((slot + 1) & kCatalogSlotMask);
            }
            return kNoIndex;
        }

        // ---------- 阶段 2：catalog 全量候选引用 ----------

        /**
         *  @brief 校验一个 catalog 条目的全部候选：未知候选、哈希碰撞、候选能力一致性、组内重复、自依赖
         *  @note 未选中的插件同样校验：catalog 的合法性不依赖本产品是否启用它。
         *        组内重复与自依赖在声明阶段（validate_group_declaration）已经先判过一次；这里的同名
         *        集合判据保留为对畸形 POD 的二次防线，分类仍是 DuplicateCandidate，不删除诊断。
         */
        template <typename Shape, typename Entry>
        constexpr void validate_entry_candidates(PlanState<Shape>& state, const Entry& entry, uint16_t index) noexcept
        {
            if (has_error(state))
            {
                return;
            }
            constexpr size_t kSeenWords = (kMaxCatalog + 31) / 32;

            const auto& declaration = entry.declaration;
            const auto& metadata    = entry.metadata;
            for (uint16_t group = 0; group < declaration.group_count; ++group)
            {
                const uint16_t begin = declaration.group_begin[group];
                const uint16_t end   = declaration.group_begin[group + 1];
                if (begin == end)
                {
                    fail(state, AssemblyPhase::Declaration, PluginError::EmptyRequirementGroup, index, group, 0, kNoIndex);
                    return;
                }
                const std::string_view capability = metadata.contracts[group].capability;
                uint32_t               seen[kSeenWords] = {};
                for (uint16_t candidate = begin; candidate < end; ++candidate)
                {
                    const uint32_t length  = declaration.candidate_length[candidate];
                    const uint64_t id      = declaration.candidate_id[candidate];
                    const uint64_t prefix0 = declaration.candidate_prefix0[candidate];
                    const uint64_t prefix1 = declaration.candidate_prefix1[candidate];
                    const char*    bytes   = declaration.candidate_pool + declaration.candidate_offset[candidate];
                    if (length == 0)
                    {
                        fail(state, AssemblyPhase::Declaration, PluginError::InvalidName, index, group, candidate, kNoIndex);
                        return;
                    }
                    if (name_equals(declaration.identity, bytes, length, id, prefix0, prefix1))
                    {
                        fail(state, AssemblyPhase::Declaration, PluginError::SelfDependency, index, group, candidate, index);
                        return;
                    }
                    uint16_t   slot      = static_cast<uint16_t>(id & kCatalogSlotMask);
                    uint16_t   found     = kNoIndex;
                    PluginError lookup   = PluginError::None;
                    for (uint16_t probe = 0; probe < kCatalogSlotCount; ++probe)
                    {
                        const uint16_t occupied = state.catalog_slot[slot];
                        if (occupied == 0)
                        {
                            break;
                        }
                        const uint16_t hit = static_cast<uint16_t>(occupied - 1);
                        if (state.catalog_key_id[hit] == id)
                        {
                            // 标量键先确认（长度 + 两个前缀字），全部相同才算命中；常规名字不进入逐字节比较
                            if (state.catalog_key_length[hit] == length &&
                                state.catalog_key_prefix0[hit] == prefix0 &&
                                state.catalog_key_prefix1[hit] == prefix1 &&
                                (length <= 16 || name_tail_equals(state.catalog_name[hit], bytes, length)))
                            {
                                found = hit;
                            }
                            else
                            {
                                lookup = PluginError::NameHashCollision;
                            }
                            break;
                        }
                        slot = static_cast<uint16_t>((slot + 1) & kCatalogSlotMask);
                    }
                    if (lookup != PluginError::None)
                    {
                        fail(state, AssemblyPhase::CatalogBinding, lookup, index, group, candidate, kNoIndex);
                        return;
                    }
                    if (found == kNoIndex)
                    {
                        fail(state, AssemblyPhase::CatalogBinding, PluginError::UnknownCandidate, index, group,
                             candidate, kNoIndex);
                        return;
                    }
                    const uint32_t word = static_cast<uint32_t>(found) >> 5;
                    const uint32_t bit  = 1u << (static_cast<uint32_t>(found) & 31u);
                    if ((seen[word] & bit) != 0)
                    {
                        fail(state, AssemblyPhase::CatalogBinding, PluginError::DuplicateCandidate, index, group,
                             candidate, found);
                        return;
                    }
                    seen[word] |= bit;

                    // 候选必须自己声明提供该需求的能力：白名单是作者声明，必须在 catalog 里成立。
                    // 先比 (长度, 数据指针) 两个标量：同一份静态能力名在生成器里共用存储，命中即免掉逐字节比较。
                    const uint16_t capability_begin = state.capability_begin[found];
                    const uint16_t capability_end   = static_cast<uint16_t>(capability_begin + state.capability_count[found]);
                    bool           provides         = false;
                    for (uint16_t entry_index = capability_begin; entry_index < capability_end; ++entry_index)
                    {
                        if (state.capability[entry_index].name == capability)
                        {
                            provides = true;
                            break;
                        }
                    }
                    if (!provides)
                    {
                        fail(state, AssemblyPhase::CatalogBinding, PluginError::CandidateCapabilityMismatch, index,
                             group, candidate, found);
                        return;
                    }
                }
            }
        }

        template <typename Shape, typename Catalog, size_t... Index>
        constexpr void validate_catalog_candidates(PlanState<Shape>& state, const Catalog& catalog,
                                                   std::index_sequence<Index...>) noexcept
        {
            uint16_t cursor = 0;
            ((validate_entry_candidates(state, std::get<Index>(catalog), cursor++)), ...);
        }

        // ---------- 阶段 3：选中集合与唯一绑定 ----------

        /**
         *  @brief 规范化选中集合：按名字字节序编号，校验未知/重复/非法/上限，并建立 catalog 反查
         */
        template <typename Shape, size_t SelectedCapacity>
        constexpr void build_active_table(PlanState<Shape>& state,
                                          const std::array<PluginIdentity, SelectedCapacity>& selected) noexcept
        {
            if (has_error(state))
            {
                return;
            }
            if (SelectedCapacity > kMaxActive)
            {
                fail(state, AssemblyPhase::Selection, PluginError::TooManyActive, kNoIndex, 0, 0, kNoIndex);
                return;
            }
            auto& result        = state.result;
            result.active_count = static_cast<uint16_t>(SelectedCapacity);

            // 只按实际 catalog 条数清理反查缓存，不遍历整个容量上限
            for (uint16_t index = 0; index < state.catalog_count; ++index)
            {
                state.active_of_catalog[index] = kNoIndex;
            }
            for (size_t index = 0; index < SelectedCapacity; ++index)
            {
                result.active_identity[index] = selected[index];
            }
            // 插入排序：canonical 编号与 Kahn tie-break 都由名字字节序决定，与输入顺序无关
            for (size_t index = 1; index < SelectedCapacity; ++index)
            {
                const PluginIdentity key = result.active_identity[index];
                int32_t              pos = static_cast<int32_t>(index) - 1;
                while (pos >= 0 && name_less(key, result.active_identity[pos]))
                {
                    result.active_identity[pos + 1] = result.active_identity[pos];
                    --pos;
                }
                result.active_identity[pos + 1] = key;
            }
            for (uint16_t active = 0; active < result.active_count; ++active)
            {
                if (!result.active_identity[active].is_valid)
                {
                    fail(state, AssemblyPhase::Selection, PluginError::InvalidActive, active, 0, 0, kNoIndex);
                    return;
                }
                bool           collision     = false;
                const uint16_t catalog_index = find_catalog_by_identity(state, result.active_identity[active], collision);
                if (collision)
                {
                    fail(state, AssemblyPhase::Selection, PluginError::NameHashCollision, active, 0, 0, kNoIndex);
                    return;
                }
                if (catalog_index == kNoIndex)
                {
                    fail(state, AssemblyPhase::Selection, PluginError::UnknownActive, active, 0, 0, kNoIndex);
                    return;
                }
                if (state.active_of_catalog[catalog_index] != kNoIndex)
                {
                    fail(state, AssemblyPhase::Selection, PluginError::DuplicateActive, active, 0, 0,
                         state.active_of_catalog[catalog_index]);
                    return;
                }
                result.active_catalog_index[active]  = catalog_index;
                state.active_of_catalog[catalog_index] = active;
            }
        }

        /**
         *  @brief 为单个 catalog 条目做逐需求唯一绑定与接口修订校验
         *  @note 先判 0/1/多命中（歧义优先于修订不足），命中唯一后才比较接口修订区间。
         */
        template <typename Shape, typename Entry>
        constexpr void bind_entry(PlanState<Shape>& state, const Entry& entry, uint16_t catalog_index) noexcept
        {
            if (has_error(state))
            {
                return;
            }
            const uint16_t active = state.active_of_catalog[catalog_index];
            if (active == kNoIndex)
            {
                return;     // 未选中：不绑定、不建图
            }
            const auto& declaration = entry.declaration;
            const auto& metadata    = entry.metadata;
            auto&       result      = state.result;

            // 直接写结果里的 provider 行：本函数任何错误都会立刻返回并留下非 None 诊断，
            // 错误结果按契约不得进入运行期绑定，因此不需要额外的 row 暂存与回填循环。
            for (uint16_t group = 0; group < declaration.group_count; ++group)
            {
                const uint16_t begin = declaration.group_begin[group];
                const uint16_t end   = declaration.group_begin[group + 1];
                uint16_t       matches         = 0;
                uint16_t       provider        = kNoIndex;
                uint16_t       second_provider = kNoIndex;
                uint16_t       first_candidate = kNoIndex;
                uint16_t       match_count     = 0;
                for (uint16_t candidate = begin; candidate < end; ++candidate)
                {
                    const uint32_t length  = declaration.candidate_length[candidate];
                    const uint64_t id      = declaration.candidate_id[candidate];
                    const uint64_t prefix0 = declaration.candidate_prefix0[candidate];
                    const uint64_t prefix1 = declaration.candidate_prefix1[candidate];
                    const char*    bytes   = declaration.candidate_pool + declaration.candidate_offset[candidate];
                    uint16_t       slot    = static_cast<uint16_t>(id & kCatalogSlotMask);
                    uint16_t       found   = kNoIndex;
                    for (uint16_t probe = 0; probe < kCatalogSlotCount; ++probe)
                    {
                        const uint16_t occupied = state.catalog_slot[slot];
                        if (occupied == 0)
                        {
                            break;
                        }
                        const uint16_t hit = static_cast<uint16_t>(occupied - 1);
                        if (state.catalog_key_id[hit] == id)
                        {
                            // 标量键确认优先：常规 13 字节名字不进入逐字节比较
                            if (state.catalog_key_length[hit] == length &&
                                state.catalog_key_prefix0[hit] == prefix0 &&
                                state.catalog_key_prefix1[hit] == prefix1 &&
                                (length <= 16 || name_tail_equals(state.catalog_name[hit], bytes, length)))
                            {
                                found = hit;
                            }
                            break;
                        }
                        slot = static_cast<uint16_t>((slot + 1) & kCatalogSlotMask);
                    }
                    if (found == kNoIndex || state.active_of_catalog[found] == kNoIndex)
                    {
                        continue;
                    }
                    ++matches;
                    const uint16_t matched_active = state.active_of_catalog[found];
                    if (first_candidate == kNoIndex)
                    {
                        first_candidate = candidate;
                        provider        = matched_active;
                    }
                    else if (second_provider == kNoIndex)
                    {
                        second_provider = matched_active;
                    }
                    if (match_count < kMaxCandidates)
                    {
                        // 记录绝对候选下标和对应 canonical active：不依赖候选序做后端选择，
                        // 但诊断必须能定位每一个命中项。
                        result.candidate_match[match_count]        = candidate;
                        result.candidate_match_active[match_count] = matched_active;
                        ++match_count;
                    }
                }
                result.candidate_match_count = match_count;
                if (matches == 0)
                {
                    // 空命中：没有候选可记录，用无候选哨兵说明，而不是恒 0
                    fail(state, AssemblyPhase::Selection, PluginError::MissingProvider, active, group, kNoIndex, kNoIndex);
                    return;
                }
                if (matches > 1)
                {
                    // 歧义：candidate 指向第一个命中候选，other 指向第二个 provider；
                    // 全部命中项已写入 candidate_match / candidate_match_active。
                    fail(state, AssemblyPhase::Selection, PluginError::AmbiguousProvider, active, group,
                         first_candidate, second_provider);
                    return;
                }
                result.candidate_match_count = 0;   // 成功绑定不保留歧义诊断数据
                // 唯一命中后才检查接口修订：唯一候选不达标是 RevisionMismatch，而不是去挑另一个候选
                const RequirementContract& contract = metadata.contracts[group];
                uint32_t min_revision = 0;
                uint32_t max_revision = 0;
                bool     has_range    = false;
                if (state.revision_in_order[catalog_index])
                {
                    // 声明阶段已逐项验证“第 i 条记录对第 i 个候选”，这里直接按下标取修订区间，
                    // 不再对每组做一次 16 条记录的名字扫描。
                    const uint16_t record_index = static_cast<uint16_t>(first_candidate - begin);
                    const CandidateRevision& record =
                        metadata.revisions[contract.revision_begin + record_index];
                    min_revision = record.min_revision;
                    max_revision = record.max_revision;
                    has_range    = true;
                }
                else
                {
                    for (uint16_t record_index = 0; record_index < contract.revision_count; ++record_index)
                    {
                        const CandidateRevision& record = metadata.revisions[contract.revision_begin + record_index];
                        if (record.name == result.active_identity[provider].view())
                        {
                            min_revision = record.min_revision;
                            max_revision = record.max_revision;
                            has_range    = true;
                            break;
                        }
                    }
                }
                if (!has_range)
                {
                    fail(state, AssemblyPhase::Declaration, PluginError::ContractCandidateMismatch, catalog_index,
                         group, 0, provider);
                    return;
                }
                const uint16_t provider_catalog  = result.active_catalog_index[provider];
                uint32_t       provider_revision = 0;
                bool           has_provision     = false;
                const uint16_t capability_begin  = state.capability_begin[provider_catalog];
                const uint16_t capability_end    = static_cast<uint16_t>(capability_begin + state.capability_count[provider_catalog]);
                for (uint16_t entry_index = capability_begin; entry_index < capability_end; ++entry_index)
                {
                    if (state.capability[entry_index].name == contract.capability)
                    {
                        provider_revision = state.capability[entry_index].api_revision;
                        has_provision     = true;
                        break;
                    }
                }
                if (!has_provision)
                {
                    fail(state, AssemblyPhase::CatalogBinding, PluginError::CandidateCapabilityMismatch, catalog_index,
                         group, 0, provider_catalog);
                    return;
                }
                if (provider_revision < min_revision || provider_revision > max_revision)
                {
                    fail(state, AssemblyPhase::Selection, PluginError::RevisionMismatch, active, group, 0, provider);
                    return;
                }
                result.provider[static_cast<size_t>(active) * kMaxRequirements + group] = provider;
            }
            result.active_requirement_count[active] = declaration.group_count;
        }

        template <typename Shape, typename Catalog, size_t... Index>
        constexpr void bind_catalog(PlanState<Shape>& state, const Catalog& catalog,
                                    std::index_sequence<Index...>) noexcept
        {
            uint16_t cursor = 0;
            ((bind_entry(state, std::get<Index>(catalog), cursor++)), ...);
        }

        // ---------- 阶段 4：能力与槽位一致性 ----------

        /**
         *  @brief 把第 index 条 provision 的能力名写入结果拥有存储
         */
        template <typename Shape>
        constexpr bool copy_provision_capability(AssemblyResult<Shape>& result, uint16_t index,
                                                 std::string_view capability) noexcept
        {
            if (capability.size() > kMaxNameBytes)
            {
                return false;
            }
            for (size_t i = 0; i < capability.size(); ++i)
            {
                result.provision_capability[index][i] = capability[i];
            }
            result.provision_capability[index][capability.size()] = '\0';
            result.provision_capability_length[index] = static_cast<uint32_t>(capability.size());
            return true;
        }

        /**
         *  @brief 交换两条 provision 的全部字段（插入排序与规范化使用）
         */
        template <typename Shape>
        constexpr void swap_provisions(AssemblyResult<Shape>& result, uint16_t left, uint16_t right) noexcept
        {
            std::swap(result.provision_owner[left], result.provision_owner[right]);
            std::swap(result.provision_slot[left], result.provision_slot[right]);
            std::swap(result.provision_api_revision[left], result.provision_api_revision[right]);
            std::swap(result.provision_capability_length[left], result.provision_capability_length[right]);
            for (size_t i = 0; i < kNameCapacity; ++i)
            {
                std::swap(result.provision_capability[left][i], result.provision_capability[right][i]);
            }
        }

        /**
         *  @brief provision 的 canonical 顺序：先按 canonical active owner，再按能力名字节序
         */
        template <typename Shape>
        constexpr bool provision_less(const AssemblyResult<Shape>& result, uint16_t left, uint16_t right) noexcept
        {
            if (result.provision_owner[left] != result.provision_owner[right])
            {
                return result.provision_owner[left] < result.provision_owner[right];
            }
            return result.provision_capability_view(left) < result.provision_capability_view(right);
        }

        /**
         *  @brief 把选中插件的 provides 扁平表规范化为 (owner, capability) 升序，并回填每插件区间
         */
        template <typename Shape>
        constexpr void canonicalize_provisions(PlanState<Shape>& state) noexcept
        {
            if (has_error(state))
            {
                return;
            }
            using Result = AssemblyResult<Shape>;
            auto& result = state.result;
            if (result.provision_count > Result::kProvisionSlots)
            {
                fail(state, AssemblyPhase::Ownership, PluginError::InvalidDeclaration, kNoIndex, 0, 0, kNoIndex);
                return;
            }
            uint16_t cursor = 0;
            for (uint16_t active = 0; active < result.active_count; ++active)
            {
                result.provision_begin[active] = cursor;
                while (cursor < result.provision_count && result.provision_owner[cursor] == active)
                {
                    ++cursor;
                }
                // 只在本插件区间内按能力名排序；owner 已由 collect_ownership_all 的 canonical 顺序分组
                const uint32_t begin = result.provision_begin[active];
                for (uint32_t index = begin + 1; index < cursor; ++index)
                {
                    uint32_t swap_cursor = index;
                    while (swap_cursor > begin &&
                           provision_less(result, static_cast<uint16_t>(swap_cursor),
                                          static_cast<uint16_t>(swap_cursor - 1)))
                    {
                        swap_provisions(result, static_cast<uint16_t>(swap_cursor),
                                        static_cast<uint16_t>(swap_cursor - 1));
                        --swap_cursor;
                    }
                }
            }
            result.provision_begin[result.active_count] = result.provision_count;
        }

        /**
         *  @brief 回填每个选中插件的 provides 扁平记录（能力名、修订、槽位、owner）
         */
        template <typename Shape>
        constexpr void collect_ownership(PlanState<Shape>& state, uint16_t catalog_index) noexcept
        {
            if (has_error(state))
            {
                return;
            }
            const uint16_t active = state.active_of_catalog[catalog_index];
            if (active == kNoIndex)
            {
                return;
            }
            using Result = AssemblyResult<Shape>;
            auto& result = state.result;
            const uint16_t begin = state.capability_begin[catalog_index];
            const uint16_t count = state.capability_count[catalog_index];
            result.active_provision_count[active] = count;
            for (uint16_t provide = 0; provide < count; ++provide)
            {
                if (result.provision_count >= Result::kProvisionSlots) [[unlikely]]
                {
                    fail(state, AssemblyPhase::Ownership, PluginError::InvalidDeclaration, active, 0, provide, kNoIndex);
                    return;
                }
                const CapabilityRef& ref = state.capability[begin + provide];
                const uint16_t       out = result.provision_count;
                result.provision_owner[out]        = active;
                result.provision_slot[out]         = ref.slot;
                result.provision_api_revision[out] = ref.api_revision;
                if (!copy_provision_capability(result, out, ref.name)) [[unlikely]]
                {
                    fail(state, AssemblyPhase::Ownership, PluginError::InvalidCapability, active, 0, provide, kNoIndex);
                    return;
                }
                ++result.provision_count;
            }
        }

        /**
         *  @brief 按 canonical active 顺序回填 provides，保证扁平表天然按 owner 分组
         *  @note 只遍历选中项；排序成本因此是每插件 P² 之和，而不是全 catalog provides 总数的平方。
         */
        template <typename Shape>
        constexpr void collect_ownership_all(PlanState<Shape>& state) noexcept
        {
            const auto& result = state.result;
            for (uint16_t active = 0; active < result.active_count; ++active)
            {
                collect_ownership(state, result.active_catalog_index[active]);
            }
        }

        /**
         *  @brief 产品级独占：同一 capability 在选中集合里最多一个提供者（即使无人使用也要检查）
         *  @note 比较的是每条 provision 自己记录的能力，不再用一个 active_slot/active_api_revision 代表整个插件。
         */
        template <typename Shape>
        constexpr void check_capability_uniqueness(PlanState<Shape>& state) noexcept
        {
            if (has_error(state))
            {
                return;
            }
            const auto& result = state.result;
            // 开放寻址表代替 O(provides²) 两两名字比较：每条 provision 只算一次 FNV + 有界探测，
            // 命中同哈希后才做完整字节确认，因此“前缀相同但名字不同”不会被误判成重复能力。
            for (uint16_t index = 0; index < result.provision_count; ++index)
            {
                const std::string_view capability = result.provision_capability_view(index);
                const uint64_t         hash       = hash_bytes(kFnvOffsetBasis, capability);
                uint16_t               slot       = static_cast<uint16_t>(hash & Shape::kCapabilitySlotMask);
                for (uint16_t probe = 0; probe < Shape::kCapabilitySlotCount; ++probe)
                {
                    const uint16_t occupied = state.capability_slot[slot];
                    if (occupied == 0)
                    {
                        state.capability_slot[slot] = static_cast<uint16_t>(index + 1);
                        break;
                    }
                    const uint16_t existing = static_cast<uint16_t>(occupied - 1);
                    if (result.provision_capability_view(existing) == capability)
                    {
                        fail(state, AssemblyPhase::Ownership, PluginError::DuplicateCapability,
                             result.provision_owner[existing], 0, existing, result.provision_owner[index]);
                        return;
                    }
                    slot = static_cast<uint16_t>((slot + 1) & Shape::kCapabilitySlotMask);
                }
            }
        }

        /**
         *  @brief 槽位归属：0..63 合法、不同能力不能占同槽、一个槽只有一个活动提供者
         *  @note 一个插件可以提供多条带槽位能力，但每条都必须占不同槽；槽位总数仍是 64。
         */
        template <typename Shape>
        constexpr void build_slot_ownership(PlanState<Shape>& state) noexcept
        {
            if (has_error(state))
            {
                return;
            }
            auto& result = state.result;
            for (uint16_t slot = 0; slot < kMaxSlots; ++slot)
            {
                result.slot_owner[slot] = kNoIndex;
            }
            for (uint16_t index = 0; index < result.provision_count; ++index)
            {
                const int16_t slot = result.provision_slot[index];
                if (slot == kNoSlot)
                {
                    continue;
                }
                if (slot < 0 || slot >= static_cast<int16_t>(kMaxSlots))
                {
                    fail(state, AssemblyPhase::Ownership, PluginError::InvalidSlot,
                         result.provision_owner[index], 0, index, kNoIndex);
                    return;
                }
                const size_t slot_index = static_cast<size_t>(slot);
                if (result.slot_owner[slot_index] != kNoIndex)
                {
                    fail(state, AssemblyPhase::Ownership, PluginError::SlotConflict,
                         result.provision_owner[index], 0, index, result.slot_owner[slot_index]);
                    return;
                }
                result.slot_owner[slot_index] = result.provision_owner[index];
            }
        }

        // ---------- 阶段 5：固定图 ----------

        /**
         *  @brief 去重边、固定图环检测、确定性 Kahn 拓扑序与真实环路径提取
         *  @note 只有选中后的固定图参与判定：未选中后端形成的潜在环不影响本产品。
         *        同一上游满足同一插件的多个需求时，绑定记录全保留，图边只计一次。
         */
        template <typename Shape>
        constexpr void build_graph(PlanState<Shape>& state) noexcept
        {
            if (has_error(state))
            {
                return;
            }
            using Result = AssemblyResult<Shape>;

            constexpr size_t kEdgeSlots = Result::kEdgeSlots;

            auto&          result = state.result;
            const uint16_t count  = result.active_count;

            constexpr size_t kSeenProviderWords = (kMaxActive + 31) / 32;
            for (uint16_t dependent = 0; dependent < count; ++dependent)
            {
                result.edge_begin[dependent]     = result.edge_count;
                const uint16_t requirement_count = result.active_requirement_count[dependent];
                uint32_t       seen_provider[kSeenProviderWords] = {};   // 每个下游一张位图：去重是 O(1)，不再回扫前面所有组
                for (uint16_t group = 0; group < requirement_count; ++group)
                {
                    const uint16_t provider = result.provider[static_cast<size_t>(dependent) * kMaxRequirements + group];
                    const uint32_t word     = static_cast<uint32_t>(provider) >> 5;
                    const uint32_t bit      = 1u << (static_cast<uint32_t>(provider) & 31u);
                    if ((seen_provider[word] & bit) != 0)
                    {
                        continue;
                    }
                    seen_provider[word] |= bit;
                    if (result.edge_count >= kEdgeSlots)
                    {
                        fail(state, AssemblyPhase::Graph, PluginError::InvalidDeclaration, dependent, group, 0, kNoIndex);
                        return;
                    }
                    result.edge_target[result.edge_count] = provider;
                    ++result.edge_count;
                    ++state.in_degree[dependent];
                }
            }
            result.edge_begin[count] = result.edge_count;

            // 正向邻接（provider -> dependent）用于 Kahn 减入度
            for (uint16_t index = 0; index <= count; ++index)
            {
                state.forward_begin[index] = 0;
            }
            for (uint16_t dependent = 0; dependent < count; ++dependent)
            {
                for (uint16_t edge = result.edge_begin[dependent]; edge < result.edge_begin[dependent + 1]; ++edge)
                {
                    ++state.forward_begin[result.edge_target[edge] + 1];
                }
            }
            for (uint16_t index = 0; index < count; ++index)
            {
                state.forward_begin[index + 1] = static_cast<uint16_t>(state.forward_begin[index + 1] + state.forward_begin[index]);
            }
            {
                uint16_t write_cursor[kMaxActive] = {};
                for (uint16_t index = 0; index < count; ++index)
                {
                    write_cursor[index] = state.forward_begin[index];
                }
                for (uint16_t dependent = 0; dependent < count; ++dependent)
                {
                    for (uint16_t edge = result.edge_begin[dependent]; edge < result.edge_begin[dependent + 1]; ++edge)
                    {
                        const uint16_t provider = result.edge_target[edge];
                        state.forward_target[write_cursor[provider]++] = dependent;
                    }
                }
            }

            // Kahn：canonical 下标本身按精确名字字节序编号，取第一个就绪节点即“最小名字”
            uint16_t order_count = 0;
            while (order_count < count)
            {
                int32_t pick = -1;
                for (uint16_t active = 0; active < count; ++active)
                {
                    if (!state.is_sorted[active] && state.in_degree[active] == 0)
                    {
                        pick = static_cast<int32_t>(active);
                        break;
                    }
                }
                if (pick < 0)
                {
                    break;
                }
                state.is_sorted[pick]            = true;
                result.topo_order[order_count++] = static_cast<uint16_t>(pick);
                for (uint16_t edge = state.forward_begin[pick]; edge < state.forward_begin[pick + 1]; ++edge)
                {
                    const uint16_t dependent = state.forward_target[edge];
                    --state.in_degree[dependent];
                }
            }

            if (order_count != count)
            {
                fail(state, AssemblyPhase::Graph, PluginError::Cycle, kNoIndex, 0, 0, kNoIndex);
                // 真实环提取（容量 = active 上限，不截断）：沿“下游 -> 上游”游走。
                // 任何未排序节点都至少有一个未排序上游（它从未把入度减到 0，而减到 0 的前提是全部上游已排序），
                // 因此从任意未排序起点出发，第一次遇到重复节点的位置就是一个真实环入口；
                // 丢掉环之前的残余前缀，剩下的成员即环本身。error_nodes[i+1] 是 error_nodes[i] 的一个上游。
                uint16_t path[kMaxActive] = {};
                for (uint16_t start = 0; start < count && !result.error_is_cycle; ++start)
                {
                    if (state.is_sorted[start])
                    {
                        continue;
                    }
                    uint16_t path_count = 0;
                    uint16_t node       = start;
                    while (path_count < kMaxActive)
                    {
                        path[path_count++] = node;
                        uint16_t previous  = kNoIndex;
                        for (uint16_t edge = result.edge_begin[node]; edge < result.edge_begin[node + 1]; ++edge)
                        {
                            if (!state.is_sorted[result.edge_target[edge]])
                            {
                                previous = result.edge_target[edge];
                                break;
                            }
                        }
                        if (previous == kNoIndex)
                        {
                            break;      // 没有任何未排序上游：纯残余下游节点，不是环成员
                        }
                        uint16_t repeat = kNoIndex;
                        for (uint16_t i = 0; i < path_count; ++i)
                        {
                            if (path[i] == previous)
                            {
                                repeat = i;
                                break;
                            }
                        }
                        if (repeat != kNoIndex)
                        {
                            const uint16_t kept = static_cast<uint16_t>(path_count - repeat);
                            for (uint16_t i = 0; i < kept; ++i)
                            {
                                result.error_nodes[i] = path[repeat + i];
                            }
                            result.error_node_count = kept;
                            result.error_is_cycle   = true;
                            break;
                        }
                        node = previous;
                    }
                }
                return;
            }
        }

        /**
         *  @brief 确定性指纹：schema + 规范化名字 + 逐 provision 修订/槽位 + 拓扑序 + 逐需求绑定 + 槽位归属
         *  @note 不含 catalog 枚举顺序、选中输入顺序、候选顺序、运行期地址或绝对路径；
         *        provides 已按 (owner, 能力名) 规范化排序，因此书写顺序也不影响指纹。
         *        每条 provision 的接口修订与槽位都单独折叠，不再由插件的单个 active_slot 代表全部能力。
         */
        template <typename Shape>
        constexpr void compute_fingerprint(PlanState<Shape>& state) noexcept
        {
            if (has_error(state))
            {
                return;
            }
            auto&    result = state.result;
            uint64_t hash   = 14695981039346656037ull;
            hash = hash_mix(hash, kSchemaRevision);
            hash = hash_mix(hash, result.active_count);
            hash = hash_mix(hash, result.edge_count);
            hash = hash_mix(hash, result.provision_count);
            for (uint16_t active = 0; active < result.active_count; ++active)
            {
                hash = hash_bytes(hash, result.active_identity[active].view());
                hash = hash_mix(hash, 0xFFu);
            }
            for (uint16_t index = 0; index < result.provision_count; ++index)
            {
                hash = hash_mix(hash, result.provision_owner[index]);
                hash = hash_bytes(hash, result.provision_capability_view(index));
                hash = hash_mix(hash, 0xFFu);
                hash = hash_mix(hash, result.provision_api_revision[index]);
                hash = hash_mix(hash, static_cast<uint64_t>(static_cast<int64_t>(result.provision_slot[index]) + 1));
            }
            for (uint16_t index = 0; index < result.active_count; ++index)
            {
                hash = hash_mix(hash, result.topo_order[index]);
            }
            for (uint16_t active = 0; active < result.active_count; ++active)
            {
                hash = hash_mix(hash, result.active_requirement_count[active]);
                for (uint16_t group = 0; group < result.active_requirement_count[active]; ++group)
                {
                    hash = hash_mix(hash, result.provider[static_cast<size_t>(active) * kMaxRequirements + group]);
                }
            }
            for (uint16_t slot = 0; slot < kMaxSlots; ++slot)
            {
                hash = hash_mix(hash, result.slot_owner[slot]);
            }
            result.fingerprint = hash;
        }

        // ---------- 阶段 6：构建证据 ----------

        template <typename Shape>
        constexpr void fail_evidence(PlanState<Shape>& state, PluginError code, uint16_t record,
                                     uint16_t plugin, uint16_t requirement, uint16_t candidate) noexcept
        {
            if (has_error(state))
            {
                return;
            }
            fail(state, AssemblyPhase::Evidence, code, plugin, requirement, candidate, kNoIndex);
            state.result.diagnostic.evidence_index = record;
        }

        /**
         *  @brief 用构建系统派生的证据交叉校验 C++ 推导结果（计划书 8.5）
         *  @note 四类证据都做**双向一一匹配**：每条证据必须消费一个尚未被消费的计划条目，
         *        因此重复记录（两条相同 provider 顶替两条不同需求、两条相同边顶替另一条边、
         *        重复的 selected 名字）必然被判为不一致，不会被数量相等掩盖。
         *        所有计数都先做容量与范围检查，越界时只返回类别、不索引数组。
         */
        template <typename Shape>
        constexpr void verify_evidence(PlanState<Shape>& state, const AssemblyInput<Shape>& input) noexcept
        {
            if (has_error(state))
            {
                return;
            }
            using Result = AssemblyResult<Shape>;

            constexpr size_t kBindingSlots = Result::kBindingSlots;
            constexpr size_t kEdgeSlots    = Result::kEdgeSlots;

            auto&       result   = state.result;
            const auto& evidence = input.evidence;

            // ---- 1. 选中集合：名字、CMake 目标、身份宏、编译身份 ----
            if (evidence.selected_count != result.active_count ||
                evidence.selected_count > Shape::kSelectedEvidenceCapacity)
            {
                fail_evidence(state, PluginError::EvidenceSelectionMismatch, 0, kNoIndex, 0, 0);
                return;
            }
            bool consumed_active[kMaxActive] = {};
            for (uint16_t record = 0; record < static_cast<uint16_t>(evidence.selected_count); ++record)
            {
                const SelectedEvidence& item   = evidence.selected[record];
                const uint16_t          active = find_active_by_name(state, item.name);
                if (active == kNoIndex || active >= result.active_count || consumed_active[active])
                {
                    fail_evidence(state, PluginError::EvidenceSelectionMismatch, record, active, 0, 0);
                    return;
                }
                consumed_active[active]           = true;
                const uint16_t catalog_index      = result.active_catalog_index[active];
                if (item.target != state.catalog_target[catalog_index])
                {
                    fail_evidence(state, PluginError::EvidenceTargetMismatch, record, active, 0, 0);
                    return;
                }
                if (!identity_macro_matches(item.target, item.macro))
                {
                    fail_evidence(state, PluginError::EvidenceMacroMismatch, record, active, 0, 0);
                    return;
                }
                if (item.plugin_id != result.active_identity[active].id)
                {
                    fail_evidence(state, PluginError::EvidenceIdentityMismatch, record, active, 0, 0);
                    return;
                }
            }

            // ---- 2. 逐需求 provider ----
            size_t expected_bindings = 0;
            for (uint16_t active = 0; active < result.active_count; ++active)
            {
                expected_bindings += result.active_requirement_count[active];
            }
            if (evidence.binding_count != expected_bindings ||
                evidence.binding_count > Shape::kBindingEvidenceCapacity)
            {
                fail_evidence(state, PluginError::EvidenceProviderMismatch, 0, kNoIndex, 0, 0);
                return;
            }
            uint32_t consumed_binding[(kBindingSlots + 31) / 32] = {};
            // 相邻记录常指向同一个下游与同一个 provider（生成器按 (下游, 需求) 顺序写出证据）。
            // 这里只缓存上一次的判定：(指针, 长度, 下标) 全等就复用结论，否则退回完整名字比较。
            // 判据没有变松——指针相等且长度相等必然字节相等，且缓存只用于完全相同的记录形态。
            const char* cached_dependent_data  = nullptr;
            size_t      cached_dependent_size  = 0;
            uint16_t    cached_active          = kNoIndex;
            const char* cached_provider_data   = nullptr;
            size_t      cached_provider_size   = 0;
            uint16_t    cached_provider_index  = kNoIndex;
            bool        cached_provider_matches = false;
            for (uint16_t record = 0; record < static_cast<uint16_t>(evidence.binding_count); ++record)
            {
                const BindingEvidence& item = evidence.bindings[record];
                if (item.dependent.data() != cached_dependent_data || item.dependent.size() != cached_dependent_size)
                {
                    cached_dependent_data = item.dependent.data();
                    cached_dependent_size = item.dependent.size();
                    cached_active         = find_active_by_name(state, item.dependent);
                }
                const uint16_t active = cached_active;
                if (active == kNoIndex || active >= result.active_count)
                {
                    fail_evidence(state, PluginError::EvidenceProviderMismatch, record, active, item.requirement, 0);
                    return;
                }
                if (item.requirement >= result.active_requirement_count[active])
                {
                    fail_evidence(state, PluginError::EvidenceProviderMismatch, record, active, item.requirement, 0);
                    return;
                }
                const size_t   slot_index = static_cast<size_t>(active) * kMaxRequirements + item.requirement;
                const uint16_t provider   = result.provider[slot_index];
                bool           provider_matches;
                if (provider == cached_provider_index && item.provider.data() == cached_provider_data &&
                    item.provider.size() == cached_provider_size)
                {
                    provider_matches = cached_provider_matches;
                }
                else
                {
                    provider_matches      = provider < result.active_count &&
                                            result.active_identity[provider].view() == item.provider;
                    cached_provider_index  = provider;
                    cached_provider_data   = item.provider.data();
                    cached_provider_size   = item.provider.size();
                    cached_provider_matches = provider_matches;
                }
                if (!provider_matches)
                {
                    fail_evidence(state, PluginError::EvidenceProviderMismatch, record, active, item.requirement, 0);
                    return;
                }
                const uint32_t word = static_cast<uint32_t>(slot_index) >> 5;
                const uint32_t bit  = 1u << (static_cast<uint32_t>(slot_index) & 31u);
                if ((consumed_binding[word] & bit) != 0)
                {
                    fail_evidence(state, PluginError::EvidenceProviderMismatch, record, active, item.requirement, 0);
                    return;
                }
                consumed_binding[word] |= bit;
            }

            // ---- 3. 真实链接边（上游 -> 下游） ----
            if (evidence.edge_count != result.edge_count || evidence.edge_count > Shape::kEdgeEvidenceCapacity)
            {
                fail_evidence(state, PluginError::EvidenceEdgeMismatch, 0, kNoIndex, 0, 0);
                return;
            }
            uint32_t consumed_edge[(kEdgeSlots + 31) / 32] = {};
            for (uint16_t record = 0; record < static_cast<uint16_t>(evidence.edge_count); ++record)
            {
                const EdgeEvidence& item      = evidence.edges[record];
                const uint16_t      dependent = find_active_by_name(state, item.to);
                if (dependent == kNoIndex || dependent >= result.active_count)
                {
                    fail_evidence(state, PluginError::EvidenceEdgeMismatch, record, dependent, 0, 0);
                    return;
                }
                bool matched = false;
                for (uint16_t edge = result.edge_begin[dependent]; edge < result.edge_begin[dependent + 1]; ++edge)
                {
                    const uint32_t word = static_cast<uint32_t>(edge) >> 5;
                    const uint32_t bit  = 1u << (static_cast<uint32_t>(edge) & 31u);
                    if ((consumed_edge[word] & bit) != 0)
                    {
                        continue;
                    }
                    if (result.active_identity[result.edge_target[edge]].view() == item.from)
                    {
                        consumed_edge[word] |= bit;
                        matched              = true;
                        break;
                    }
                }
                if (!matched)
                {
                    fail_evidence(state, PluginError::EvidenceEdgeMismatch, record, dependent, 0, 0);
                    return;
                }
            }

            // ---- 4. 槽位归属：每条带槽位 provision 都要有唯一证据（一个插件可以有多条） ----
            size_t expected_slots = 0;
            for (uint16_t index = 0; index < result.provision_count; ++index)
            {
                if (result.provision_slot[index] != kNoSlot)
                {
                    ++expected_slots;
                }
            }
            if (evidence.slot_count != expected_slots || evidence.slot_count > Shape::kSlotEvidenceCapacity ||
                evidence.slot_count > kMaxSlots)
            {
                fail_evidence(state, PluginError::EvidenceSlotMismatch, 0, kNoIndex, 0, 0);
                return;
            }
            constexpr size_t kProvisionSlots = Result::kProvisionSlots;
            uint32_t consumed_slot[(kProvisionSlots + 31) / 32] = {};
            for (uint16_t record = 0; record < static_cast<uint16_t>(evidence.slot_count); ++record)
            {
                const SlotEvidence& item   = evidence.slots[record];
                const uint16_t      active = find_active_by_name(state, item.owner);
                if (active == kNoIndex || active >= result.active_count)
                {
                    fail_evidence(state, PluginError::EvidenceSlotMismatch, record, active, 0, 0);
                    return;
                }
                if (item.slot < 0 || item.slot >= static_cast<int16_t>(kMaxSlots))
                {
                    fail_evidence(state, PluginError::EvidenceSlotMismatch, record, active, 0, 0);
                    return;
                }
                bool matched = false;
                for (uint16_t index = result.provision_begin[active];
                     index < result.provision_begin[static_cast<uint16_t>(active + 1)]; ++index)
                {
                    if (result.provision_slot[index] == kNoSlot)
                    {
                        continue;
                    }
                    const uint32_t word = static_cast<uint32_t>(index) >> 5;
                    const uint32_t bit  = 1u << (static_cast<uint32_t>(index) & 31u);
                    if ((consumed_slot[word] & bit) != 0)
                    {
                        continue;
                    }
                    if (result.provision_capability_view(index) != item.capability ||
                        result.provision_slot[index] != item.slot ||
                        result.slot_owner[item.slot] != active)
                    {
                        continue;
                    }
                    consumed_slot[word] |= bit;
                    matched              = true;
                    break;
                }
                if (!matched)
                {
                    fail_evidence(state, PluginError::EvidenceSlotMismatch, record, active, 0, 0);
                    return;
                }
            }
        }

        // ---------- 阶段 7：运行期绑定元数据 ----------

        /**
         *  @brief 结构比对运行期绑定记录：每个选中插件的每条 provides 都必须有唯一的 (名字, 能力, 身份, 修订) 记录
         *  @note 本阶段不生成 PluginRuntime；它只证明“运行期将写入的身份/修订元数据”与编译期计划一致。
         */
        template <typename Shape>
        constexpr void verify_runtime_bindings(PlanState<Shape>& state, const AssemblyInput<Shape>& input) noexcept
        {
            if (has_error(state))
            {
                return;
            }
            const auto& result   = state.result;
            const auto& evidence = input.evidence;

            using ResultType = AssemblyResult<Shape>;

            // 每个选中插件的每条 provides（含无槽位能力）都必须有唯一的运行期记录
            const size_t expected = result.provision_count;
            if (evidence.runtime_count != expected || evidence.runtime_count > Shape::kRuntimeEvidenceCapacity)
            {
                fail_evidence(state, PluginError::RuntimeBindingMismatch, 0, kNoIndex, 0, 0);
                return;
            }
            constexpr size_t kProvisionSlots = ResultType::kProvisionSlots;
            uint32_t         consumed[(kProvisionSlots + 31) / 32] = {};
            for (uint16_t record = 0; record < static_cast<uint16_t>(evidence.runtime_count); ++record)
            {
                const RuntimeBindingEvidence& item   = evidence.runtime[record];
                const uint16_t                active = find_active_by_name(state, item.name);
                if (active == kNoIndex || active >= result.active_count)
                {
                    fail_evidence(state, PluginError::RuntimeBindingMismatch, record, active, 0, 0);
                    return;
                }
                if (item.plugin_id != result.active_identity[active].id)
                {
                    fail_evidence(state, PluginError::RuntimeBindingMismatch, record, active, 0, 0);
                    return;
                }
                bool matched = false;
                for (uint16_t index = result.provision_begin[active];
                     index < result.provision_begin[static_cast<uint16_t>(active + 1)]; ++index)
                {
                    const uint32_t word = static_cast<uint32_t>(index) >> 5;
                    const uint32_t bit  = 1u << (static_cast<uint32_t>(index) & 31u);
                    if ((consumed[word] & bit) != 0)
                    {
                        continue;
                    }
                    if (result.provision_capability_view(index) != item.capability)
                    {
                        continue;
                    }
                    if (result.provision_api_revision[index] != item.api_revision)
                    {
                        fail_evidence(state, PluginError::RuntimeBindingMismatch, record, active, 0, index);
                        return;
                    }
                    consumed[word] |= bit;
                    matched       = true;
                    break;
                }
                if (!matched)
                {
                    fail_evidence(state, PluginError::RuntimeBindingMismatch, record, active, 0, 0);
                    return;
                }
            }
        }

        // ---------- 总装 ----------

        /**
         *  @brief 阶段 1-2：声明/catalog 全量候选校验，不依赖 active 容量
         *  @note 先于 active 容量检查执行，保证“catalog 非法”不会被“Shape active > 128”提前掩盖。
         */
        template <typename Shape, size_t... Index>
        constexpr void prepare_catalog(PlanState<Shape>& state, const AssemblyInput<Shape>& input,
                                       std::index_sequence<Index...> indices) noexcept
        {
            state.catalog_count        = static_cast<uint16_t>(sizeof...(Index));
            state.result.catalog_count = static_cast<uint16_t>(sizeof...(Index));

            collect_catalog(state, input.catalog, indices);
            build_identity_index(state);
            validate_catalog_candidates(state, input.catalog, indices);
        }

        /**
         *  @brief 阶段 3-7：绑定、归属、固定图、指纹、证据与运行期元数据
         *  @note 结果由调用方持有、本函数只写它（state.result 是引用），返回路径上不再复制整份定长结果。
         */
        template <typename Shape, size_t... Index>
        constexpr void finish_plan(PlanState<Shape>& state, const AssemblyInput<Shape>& input,
                                   std::index_sequence<Index...> indices) noexcept
        {
            build_active_table(state, input.selected);
            bind_catalog(state, input.catalog, indices);
            collect_ownership_all(state);
            canonicalize_provisions(state);
            check_capability_uniqueness(state);
            build_slot_ownership(state);
            build_graph(state);
            compute_fingerprint(state);
            verify_evidence(state, input);
            verify_runtime_bindings(state, input);
        }

        /**
         *  @brief 第三个操作族实现：schema/catalog 容量先判，catalog 内容次之，active 容量最后
         *  @param input 完整 catalog + 选中集合 + 构建证据
         *  @return 组装结果；任何阶段的首条错误都会短期返回并保留诊断
         *  @note 阶段优先级与头文件注释一致：Declaration -> CatalogBinding -> Selection -> Ownership -> Graph
         *        -> Evidence -> RuntimeMetadata。Shape::kActiveCapacity > 128 属于 Selection，必须在 catalog
         *        声明/候选校验之后才报，不能先于更早阶段的结构错误。
         */
        template <typename Shape>
        constexpr AssemblyResult<Shape> sort_dispatch(const AssemblyInput<Shape>& input) noexcept
        {
            using Catalog = typename Shape::CatalogTypeAlias;
            constexpr size_t kCatalogSize = std::tuple_size_v<Catalog>;

            if (input.schema_revision != kSchemaRevision)
            {
                AssemblyResult<Shape> result{};
                result.diagnostic.code  = PluginError::SchemaMismatch;
                result.diagnostic.phase = AssemblyPhase::Declaration;
                return result;
            }
            else if constexpr (kCatalogSize > kMaxCatalog)
            {
                AssemblyResult<Shape> result{};
                result.diagnostic.code  = PluginError::TooManyCatalog;
                result.diagnostic.phase = AssemblyPhase::Declaration;
                return result;
            }
            else
            {
                // 结果对象只在这里构造一次，PlanState 通过引用写它，返回时由 NRVO 交给调用方
                AssemblyResult<Shape> result{};
                PlanState<Shape>      state{ result };
                constexpr auto        indices = std::make_index_sequence<kCatalogSize>{};
                prepare_catalog(state, input, indices);
                if (has_error(state))
                {
                    return result;
                }
                if constexpr (Shape::kActiveCapacity > kMaxActive)
                {
                    fail(state, AssemblyPhase::Selection, PluginError::TooManyActive, kNoIndex, 0, 0, kNoIndex);
                    return result;
                }
                else
                {
                    finish_plan(state, input, indices);
                    return result;
                }
            }
        }

    } // namespace detail
} // namespace ID::PluginCompileTime
