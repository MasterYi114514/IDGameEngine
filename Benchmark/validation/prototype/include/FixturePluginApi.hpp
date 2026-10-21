#pragma once

#include <array>
#include <string_view>
#include <tuple>
#include <type_traits>

#include "Kernel/CoreTypes.hpp"

/**
 *  P0 原型：编译期插件声明与组装（fixture 版，不进入 ID 生产内核）
 *  1. 对外只有计划书第 5 节界定的三个 consteval 操作族：
 *     register_plugin（注册身份）/ declare_dependencies（声明依赖）/ sort_and_validate（排序与校验）。
 *     compare_evidence 是纯 constexpr 比对助手，对应计划书 8.5「第三个接口重新推导后逐项比较证据」，
 *     不是第四个构建入口，也不产生任何隐式全局状态。
 *  2. 所有返回数据自带拥有存储：PluginIdentity 复制名字字节，AssemblyResult 只存身份、索引与诊断，
 *     不保存指向常量求值局部对象或自身成员的指针，因此复制/移动不会产生悬空 view。
 *  3. 全部操作 consteval：只能在常量求值中调用，编译单元顺序、链接顺序都不构成隐式状态。
 *  4. 本头是 Benchmark 校验资产的 fixture 实现，用来在目标编译器上固定 API 形状与语义；
 *     P1 才把同等语义搬进 ID/include/Kernel，并补齐能力/槽位/接口修订等完整契约。
 */

namespace ID::Bench::Fixture
{
    // 计划书 5.2 / 6.5 的容量预算：P0 用来做边界与压力验证，不是已冻结的 ABI 常量
    inline constexpr size_t   kMaxNameBytes          = 96;      // 名字最大字节数（不含末尾 NUL）
    inline constexpr size_t   kNameCapacity          = kMaxNameBytes + 1;
    inline constexpr size_t   kMaxCatalog            = 256;     // catalog 上限（独立于 active 上限）
    inline constexpr size_t   kMaxActive             = 128;     // 产品选中插件上限
    inline constexpr size_t   kMaxRequirements       = 32;      // 每插件需求组上限
    inline constexpr size_t   kMaxCandidatesPerGroup = 16;      // 每个需求组的候选上限
    inline constexpr uint16_t kNoIndex               = 0xFFFF;  // 无下标哨兵

    /**
     *  @brief 组装期的结构化错误类别
     *  @note 每个类别对应计划书 7 的一条校验项；反向用例按枚举值断言错误类别，
     *        因此新增/改名必须同步更新 Benchmark/validation 下的校验用例。
     */
    enum class ErrorCode : uint8_t
    {
        None = 0,                       // 无错误
        InvalidName,                    // 名字为空/超长/首字符非字母/含非法字符
        DuplicatePlugin,                // catalog 内出现同名插件
        NameHashCollision,              // 名字不同但 64 位 ID 相同（碰撞模拟）
        InvalidDeclaration,             // 声明容量或内部不变量被写坏
        EmptyRequirementGroup,          // 空内层：某需求组没有任何候选
        DuplicateCandidate,             // 同一需求组里重复候选
        SelfDependency,                 // 候选包含所属插件自身
        UnknownCandidate,               // 候选名字不在完整 catalog 中
        DuplicateActive,                // 选中集合里重复出现同一插件
        UnknownActive,                  // 选中集合里出现 catalog 之外的插件
        TooManyActive,                  // 选中插件数超过 128
        TooManyCatalog,                 // catalog 条目数超过 256
        TooManyRequirements,            // 某插件需求组数超过 32
        TooManyCandidates,              // 某需求组候选数超过 16
        MissingProvider,                // 某需求在选中集合内 0 个候选
        AmbiguousProvider,              // 某需求在选中集合内 >=2 个候选
        Cycle,                          // 选中后的固定图存在环
        EvidenceSelectionMismatch,      // 构建证据的选中集合与 C++ 推导不一致
        EvidenceProviderMismatch,       // 构建证据的逐需求 provider 与 C++ 推导不一致
        EvidenceEdgeMismatch,           // 构建证据的链接边与 C++ 推导不一致
        MaxErrors
    };

    /**
     *  @brief 错误类别的稳定文本，用于诊断与测试输出
     *  @param code 错误类别
     *  @return 形如 "AmbiguousProvider" 的静态字符串
     */
    constexpr std::string_view error_name(ErrorCode code) noexcept
    {
        switch (code)
        {
            case ErrorCode::None:                       return "None";
            case ErrorCode::InvalidName:                return "InvalidName";
            case ErrorCode::DuplicatePlugin:            return "DuplicatePlugin";
            case ErrorCode::NameHashCollision:          return "NameHashCollision";
            case ErrorCode::InvalidDeclaration:         return "InvalidDeclaration";
            case ErrorCode::EmptyRequirementGroup:      return "EmptyRequirementGroup";
            case ErrorCode::DuplicateCandidate:         return "DuplicateCandidate";
            case ErrorCode::SelfDependency:             return "SelfDependency";
            case ErrorCode::UnknownCandidate:           return "UnknownCandidate";
            case ErrorCode::DuplicateActive:            return "DuplicateActive";
            case ErrorCode::UnknownActive:              return "UnknownActive";
            case ErrorCode::TooManyActive:              return "TooManyActive";
            case ErrorCode::TooManyCatalog:             return "TooManyCatalog";
            case ErrorCode::TooManyRequirements:        return "TooManyRequirements";
            case ErrorCode::TooManyCandidates:          return "TooManyCandidates";
            case ErrorCode::MissingProvider:            return "MissingProvider";
            case ErrorCode::AmbiguousProvider:          return "AmbiguousProvider";
            case ErrorCode::Cycle:                      return "Cycle";
            case ErrorCode::EvidenceSelectionMismatch:  return "EvidenceSelectionMismatch";
            case ErrorCode::EvidenceProviderMismatch:   return "EvidenceProviderMismatch";
            case ErrorCode::EvidenceEdgeMismatch:       return "EvidenceEdgeMismatch";
            case ErrorCode::MaxErrors:                  return "MaxErrors";
        }
        return "Unknown";
    }

    /**
     *  @brief 插件编译期身份（计划书 5.2）
     *  @note 名字以定长拥有存储复制进来，不同长度的名字可以放进同一类型数组；
     *        结构体只含 POD，可平凡复制，复制后不引用原对象的任何存储。
     */
    struct PluginIdentity
    {
        char        name[kNameCapacity] = {};       // 拥有存储的名字（以 NUL 结尾）
        uint64_t    prefix0             = 0;        // 前 8 字节（小端拼接，不足补 0），用于加速恒等比较
        uint64_t    prefix1             = 0;        // 第 8..15 字节（同上）
        uint64_t    id                  = 0;        // FNV-1a 64 位插件 ID
        uint32_t    length              = 0;        // 名字字节数（不含末尾 NUL）
        bool        is_valid            = false;    // 名字格式是否通过校验

        constexpr std::string_view view() const noexcept
        {
            return { name, length };
        }
    };

    namespace detail
    {
        consteval bool is_ascii_alpha(char c) noexcept
        {
            return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
        }

        consteval bool is_ascii_digit(char c) noexcept
        {
            return c >= '0' && c <= '9';
        }

        consteval bool is_name_tail(char c) noexcept
        {
            return is_ascii_alpha(c) || is_ascii_digit(c) || c == '.' || c == '_' || c == '-';
        }

        /**
         *  @brief 从字符缓冲区建立身份：校验格式、复制名字、计算 FNV-1a 64
         *  @param text 字符起始地址，长度至少 length 字节
         *  @param length 名字字节数（不含末尾 NUL）
         *  @return 合法时 is_valid 为真且名字已复制；非法时返回 is_valid 为假的空身份
         *  @note 生成器与压力输入共用的内部构造入口，不是第四个公共操作；
         *        生产 register_plugin 只提供 const char(&)[N] 形态。
         */
        consteval PluginIdentity identity_from_chars(const char* text, size_t length) noexcept
        {
            PluginIdentity identity{};
            if (text == nullptr || length == 0 || length > kMaxNameBytes || !is_ascii_alpha(text[0]))
            {
                return identity;
            }
            // 单趟完成"格式校验 + 名字复制 + 16 字节前缀 + FNV-1a 64"：
            // 常量求值按迭代/语句计数，四趟循环合并成一趟是声明阶段最直接的降本手段。
            uint64_t hash = 14695981039346656037ull;
            for (size_t i = 0; i < length; ++i)
            {
                const unsigned char byte = static_cast<unsigned char>(text[i]);
                if (i != 0 && !is_name_tail(text[i]))
                {
                    return identity;      // 非法字符：整块作废，返回到此为止的默认身份
                }
                identity.name[i] = text[i];
                hash ^= byte;
                hash *= 1099511628211ull;
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
            identity.id          = hash;          // 与 ID::hash_fnv_1a64 同一算法，此处内联避免第二趟遍历
            identity.is_valid    = true;
            return identity;
        }

        /**
         *  @brief 把名字字节（尚未构造身份）与已有身份做完整比较
         *  @param self 已构造的身份（拥有存储）
         *  @param bytes 名字字节起始地址
         *  @param length 名字字节数
         *  @param id 名字的 FNV-1a 64
         *  @param prefix0 前 8 字节
         *  @param prefix1 第 8..15 字节
         *  @return 名字完全相同时为真
         *  @note 先比标量键，键全同才逐字节确认；"64 位哈希与前缀相同但名字不同"必须判为不相等，
         *        否则真实哈希碰撞会被误判成自依赖（或误判成同名插件）。
         */
        consteval bool name_equals(const PluginIdentity& self, const char* bytes, size_t length,
                                   uint64_t id, uint64_t prefix0, uint64_t prefix1) noexcept
        {
            if (self.length != length || self.id != id || self.prefix0 != prefix0 || self.prefix1 != prefix1)
            {
                return false;
            }
            // length <= 16 时前缀字已经覆盖全部字节（不足部分两侧都是 0），无需再比
            for (size_t i = 16; i < length; ++i)
            {
                if (self.name[i] != bytes[i])
                {
                    return false;
                }
            }
            return true;
        }

        // 名字完全相等（长度 + ID + 前 16 字节 + 超出部分的字节）；哈希相同不算相等，碰撞必须走独立诊断
        consteval bool same_name(const PluginIdentity& left, const PluginIdentity& right) noexcept
        {
            if (left.length != right.length || left.id != right.id || left.prefix0 != right.prefix0 ||
                left.prefix1 != right.prefix1)
            {
                return false;
            }
            for (uint32_t i = 16; i < left.length; ++i)
            {
                if (left.name[i] != right.name[i])
                {
                    return false;
                }
            }
            return true;
        }

        // 名字按字节序严格小于；Kahn 的确定性 tie-break 依据
        consteval bool name_less(const PluginIdentity& left, const PluginIdentity& right) noexcept
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

        // 把 value 写成十进制数字到 out，返回写入字节数
        consteval size_t write_decimal(char* out, size_t value) noexcept
        {
            char   digits[20] = {};
            size_t count      = 0;
            do
            {
                digits[count++] = static_cast<char>('0' + (value % 10));
                value /= 10;
            } while (value != 0);
            for (size_t i = 0; i < count; ++i)
            {
                out[i] = digits[count - 1 - i];
            }
            return count;
        }

        // FNV-1a 续算：用于把拓扑序与绑定关系折进确定性指纹
        consteval uint64_t hash_mix(uint64_t hash, uint64_t value) noexcept
        {
            hash ^= value;
            hash *= 1099511628211ull;
            return hash;
        }
    } // namespace detail

    /**
     *  @brief 编译期注册一个插件身份（计划书 5.2）
     *  @param name 编译期字符数组或字符串字面量；必须非空、首字符为字母、只含字母数字与 . _ -
     *  @return 拥有存储的 PluginIdentity；格式非法时 is_valid 为假，不 abort、不抛异常
     *  @note consteval：运行期调用会直接编译失败，因此不可能出现"运行期改写单例"的用法
     */
    template <size_t N>
    consteval PluginIdentity register_plugin(const char (&name)[N]) noexcept
    {
        return detail::identity_from_chars(name, N - 1);
    }

    /**
     *  @brief 一个插件的不可变声明（计划书 5.3）
     *  @tparam GroupCount 需求组数（外层 AND）
     *  @tparam CandidateCount 候选名总数（各行内层 OR 的叶子总数）
     *  @note group_begin[i]..group_begin[i+1] 是第 i 组在候选表里的区间；
     *        error 只记录第一条违约，后续候选仍被完整遍历以保证计数稳定。
     *  @note 候选使用紧凑拥有存储（SoA）：逐候选只保留 (长度, 池内偏移, ID, 两个前缀字) 标量，
     *        名字字节按实际长度写进 candidate_pool；池按"候选上限 × 名字上限"预留，
     *        这是拥有语义下的最坏情况上界，不会为短名字放大，也不会退化成悬空 view 或单纯哈希。
     *        拆成标量数组后，候选校验路径只碰 30 字节，不再复制 144 字节的身份结构。
     */
    template <size_t GroupCount, size_t CandidateCount>
    struct PluginDeclaration
    {
        static inline constexpr size_t kGroupCapacity     = GroupCount;
        static inline constexpr size_t kCandidateCapacity = CandidateCount;
        static inline constexpr size_t kCandidateSlots    = CandidateCount > 0 ? CandidateCount : 1;
        static inline constexpr size_t kPoolBytes         = CandidateCount * kMaxNameBytes;

        PluginIdentity identity{};                                                 // 所属插件身份
        ErrorCode      error           = ErrorCode::None;                          // 第一条错误
        uint16_t       group_count     = 0;                                        // 实际需求组数（正常等于 GroupCount）
        uint16_t       candidate_count = 0;                                        // 实际候选数（正常等于 CandidateCount）
        uint16_t       error_group     = 0;                                        // 出错的需求组下标
        uint16_t       error_candidate = 0;                                        // 出错的候选下标
        uint16_t       group_begin[GroupCount + 1] = {};                           // 每组在候选表的区间起点
        uint16_t       candidate_length[kCandidateSlots]   = {};                   // 候选名字字节数
        uint32_t       candidate_offset[kCandidateSlots + 1] = {};                 // 候选名字在池内的起点（末位 = 池内已用字节）
        uint64_t       candidate_id[kCandidateSlots]       = {};                   // 候选名字 FNV-1a 64
        uint64_t       candidate_prefix0[kCandidateSlots]  = {};                   // 候选前 8 字节
        uint64_t       candidate_prefix1[kCandidateSlots]  = {};                   // 候选第 8..15 字节
        char           candidate_pool[kPoolBytes > 0 ? kPoolBytes : 1] = {};       // 候选名字拥有存储

        /**
         *  @brief 第 index 个候选的名字视图（指向自身拥有存储，随声明一起复制）
         *  @param index 候选下标，必须小于 candidate_count
         *  @return 名字视图；复制声明不会让视图失效
         */
        constexpr std::string_view candidate_view(size_t index) const noexcept
        {
            return { candidate_pool + candidate_offset[index], candidate_length[index] };
        }
    };

    namespace detail
    {
        template <typename Group>
        struct group_size;

        template <typename Element, size_t Count>
        struct group_size<std::array<Element, Count>>
        {
            static inline constexpr size_t value = Count;
        };

        template <size_t GroupCount, size_t CandidateCount>
        consteval void set_declaration_error(PluginDeclaration<GroupCount, CandidateCount>& declaration,
                                             ErrorCode code, size_t group, size_t candidate) noexcept
        {
            if (declaration.error == ErrorCode::None)
            {
                declaration.error           = code;
                declaration.error_group     = static_cast<uint16_t>(group);
                declaration.error_candidate = static_cast<uint16_t>(candidate);
            }
        }

        /**
         *  @brief 追加一个候选：校验格式、算标量键、把名字字节写进拥有池
         *  @note 组内重复不在这里做 O(k^2) 前缀扫描：sort_and_validate 的绑定阶段用 catalog 下标位图
         *        逐组判定重复（O(k)），语义相同且诊断类别仍是 DuplicateCandidate；
         *        这样声明阶段每个候选只剩一趟循环。
         */
        template <size_t GroupCount, size_t CandidateCount>
        consteval void add_candidate(PluginDeclaration<GroupCount, CandidateCount>& declaration,
                                     std::string_view name, size_t group_index) noexcept
        {
            if (declaration.candidate_count >= CandidateCount)
            {
                set_declaration_error(declaration, ErrorCode::InvalidDeclaration, group_index, declaration.candidate_count);
                return;
            }
            const size_t index = declaration.candidate_count;
            const size_t length = name.size();
            if (name.data() == nullptr || length == 0 || length > kMaxNameBytes || !is_ascii_alpha(name[0]))
            {
                set_declaration_error(declaration, ErrorCode::InvalidName, group_index, index);
                return;
            }
            uint64_t hash     = 14695981039346656037ull;
            uint64_t prefix0  = 0;
            uint64_t prefix1  = 0;
            bool     is_valid = true;
            for (size_t i = 0; i < length; ++i)
            {
                const unsigned char byte = static_cast<unsigned char>(name[i]);
                if (i != 0 && !is_name_tail(name[i]))
                {
                    is_valid = false;
                    break;
                }
                declaration.candidate_pool[declaration.candidate_offset[index] + i] = name[i];
                hash ^= byte;
                hash *= 1099511628211ull;
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
                set_declaration_error(declaration, ErrorCode::InvalidName, group_index, index);
                return;
            }
            declaration.candidate_length[index]  = static_cast<uint16_t>(length);
            declaration.candidate_offset[index + 1] = declaration.candidate_offset[index] + static_cast<uint32_t>(length);
            declaration.candidate_id[index]      = hash;
            declaration.candidate_prefix0[index] = prefix0;
            declaration.candidate_prefix1[index] = prefix1;
            if (name_equals(declaration.identity, declaration.candidate_pool + declaration.candidate_offset[index],
                            length, hash, prefix0, prefix1))
            {
                // 完整名字相等才算自依赖；单纯哈希/前缀相同是碰撞，留给绑定阶段诊断
                set_declaration_error(declaration, ErrorCode::SelfDependency, group_index, index);
            }
            ++declaration.candidate_count;
        }

        template <size_t GroupCount, size_t CandidateCount, typename Group>
        consteval void add_group(PluginDeclaration<GroupCount, CandidateCount>& declaration, const Group& group) noexcept
        {
            const size_t group_index = declaration.group_count;
            declaration.group_begin[group_index] = declaration.candidate_count;
            if (group_index >= kMaxRequirements)
            {
                // 结构上限：第 33 组开始只记录诊断，不再展开，避免把越界组写进结果索引
                set_declaration_error(declaration, ErrorCode::TooManyRequirements, group_index, 0);
                ++declaration.group_count;
                declaration.group_begin[declaration.group_count] = declaration.candidate_count;
                return;
            }
            if (group.size() == 0)
            {
                set_declaration_error(declaration, ErrorCode::EmptyRequirementGroup, group_index, 0);
            }
            if (group.size() > kMaxCandidatesPerGroup)
            {
                set_declaration_error(declaration, ErrorCode::TooManyCandidates, group_index, 0);
            }
            for (std::string_view name : group)
            {
                add_candidate(declaration, name, group_index);
            }
            ++declaration.group_count;
            declaration.group_begin[declaration.group_count] = declaration.candidate_count;
        }

        template <size_t GroupCount, size_t CandidateCount, typename Tuple, size_t... Index>
        consteval void add_tuple_groups(PluginDeclaration<GroupCount, CandidateCount>& declaration,
                                        const Tuple& groups, std::index_sequence<Index...>) noexcept
        {
            (add_group(declaration, std::get<Index>(groups)), ...);
        }
    } // namespace detail

    /**
     *  @brief 声明依赖：外层 tuple = AND，每个元素是一组候选（OR），各组长度可以不同（计划书 5.3）
     *  @param identity 所属插件身份，必须是 register_plugin 的返回值
     *  @param groups tuple<array<string_view,K0>, array<string_view,K1>, ...>；空 tuple 表示没有上游
     *  @return 拥有存储的 PluginDeclaration<组数, 候选总数>
     *  @note 空内层、组内重复、自依赖、非法名字都记到 error，不 abort；未知候选在 sort_and_validate 里诊断
     */
    template <typename Identity, typename... Groups>
    consteval auto declare_dependencies(Identity identity, const std::tuple<Groups...>& groups) noexcept
        -> PluginDeclaration<sizeof...(Groups), (detail::group_size<Groups>::value + ... + 0)>
    {
        constexpr size_t kGroups     = sizeof...(Groups);
        constexpr size_t kCandidates = (detail::group_size<Groups>::value + ... + 0);

        PluginDeclaration<kGroups, kCandidates> declaration{};
        declaration.identity = identity;
        if (!identity.is_valid)
        {
            detail::set_declaration_error(declaration, ErrorCode::InvalidName, 0, 0);
            return declaration;
        }
        detail::add_tuple_groups(declaration, groups, std::index_sequence_for<Groups...>{});
        return declaration;
    }

    /**
     *  @brief 声明依赖的矩形便捷重载（计划书 5.3 允许的同一接口形态）
     *  @param identity 所属插件身份
     *  @param groups array<array<string_view,K>,G>：G 个需求组，每组恰好 K 个候选
     *  @return 拥有存储的 PluginDeclaration<G, G*K>
     *  @note 语义与 tuple 形态完全一致；G 或 K 为 0 时同样成立（显式给出元素类型的空数组）
     */
    template <typename Identity, typename Element, size_t Candidates, size_t Groups>
    consteval auto declare_dependencies(Identity identity,
                                        const std::array<std::array<Element, Candidates>, Groups>& groups) noexcept
        -> PluginDeclaration<Groups, Groups * Candidates>
    {
        PluginDeclaration<Groups, Groups * Candidates> declaration{};
        declaration.identity = identity;
        if (!identity.is_valid)
        {
            detail::set_declaration_error(declaration, ErrorCode::InvalidName, 0, 0);
            return declaration;
        }
        for (size_t group_index = 0; group_index < Groups; ++group_index)
        {
            detail::add_group(declaration, groups[group_index]);
        }
        return declaration;
    }

    /**
     *  @brief 编译期组装结果（计划书 5.4）
     *  @tparam ActiveCapacity 选中插件容量（由输入数组长度推导）
     *  @tparam EdgeCapacity 去重边容量（= ActiveCapacity * kMaxRequirements）
     *  @tparam RequirementCapacity 每插件需求组容量
     *  @note 只含身份、索引与诊断；不携带任何指向常量求值局部对象的指针
     */
    template <size_t ActiveCapacity, size_t EdgeCapacity, size_t RequirementCapacity>
    struct AssemblyResult
    {
        static inline constexpr size_t kActiveCapacity      = ActiveCapacity;
        static inline constexpr size_t kEdgeCapacity        = EdgeCapacity;
        static inline constexpr size_t kRequirementCapacity = RequirementCapacity;

        static inline constexpr size_t kActiveSlots = ActiveCapacity > 0 ? ActiveCapacity : 1;
        static inline constexpr size_t kEdgeSlots   = EdgeCapacity > 0 ? EdgeCapacity : 1;
        static inline constexpr size_t kBindSlots   = ActiveCapacity * RequirementCapacity > 0
                                                      ? ActiveCapacity * RequirementCapacity : 1;

        ErrorCode   error               = ErrorCode::None;   // 第一条错误
        bool        error_is_cycle      = false;             // error_nodes 是否是真实环路径
        uint16_t    error_node_count    = 0;                 // error_nodes 的有效个数（上限 kMaxActive）
        uint16_t    error_plugin        = kNoIndex;          // 出错的下游（canonical active 下标）
        uint16_t    error_requirement   = 0;                 // 出错的需求组
        uint16_t    error_candidate     = 0;                 // 出错的候选

        uint16_t    active_count        = 0;                 // 规范化后的选中插件数
        uint16_t    edge_count          = 0;                 // 去重后的真实依赖边数
        // 环路径：error_nodes[i+1] 是 error_nodes[i] 的一个上游，末尾回到 error_nodes[0]；
        // 容量等于 active 上限，真实环不会被截断，也不把环下游的残余节点算进成员
        uint16_t    error_nodes[kActiveSlots] = {};

        PluginIdentity active_identity[kActiveSlots] = {};    // canonical active 身份（按名字排序）
        uint16_t    active_catalog_index[kActiveSlots] = {};  // canonical -> catalog
        uint16_t    active_requirement[kActiveSlots]  = {};   // 每个 active 的实际需求组数
        uint16_t    topo_order[kActiveSlots] = {};            // canonical 拓扑序
        uint16_t    provider[kBindSlots] = {};                // (active, requirement) -> provider；仅前 active_requirement 项有效
        uint16_t    edge_begin[ActiveCapacity + 1] = {};      // 每个下游的边区间起点
        uint16_t    edge_target[kEdgeSlots] = {};             // 上游的 canonical 下标
        uint64_t    fingerprint = 0;                          // 确定性指纹（名字 + 顺序 + 绑定）
    };

    // ---------- 构建证据（由 CMake 解析结果生成，供 C++ 侧交叉校验） ----------

    struct RequirementBindingEvidence
    {
        std::string_view dependent   = {};      // 下游插件名
        std::string_view provider     = {};     // 被选中的提供者名
        uint16_t         requirement = 0;       // 需求组下标
    };

    struct LinkEdgeEvidence
    {
        std::string_view from = {};             // 上游插件名
        std::string_view to   = {};             // 下游插件名
    };

    /**
     *  @brief 构建系统派生的证据（计划书 5.4 第 3 项 / 8.5）
     *  @tparam SelectedCapacity 选中集合容量
     *  @tparam BindingCapacity 逐需求 provider 记录容量
     *  @tparam EdgeCapacity 链接边容量
     *  @note 成员是 string_view：只用于常量求值期比对，不进入运行期计划
     */
    template <size_t SelectedCapacity, size_t BindingCapacity, size_t EdgeCapacity>
    struct AssemblyEvidence
    {
        static inline constexpr size_t kSelectedCapacity = SelectedCapacity > 0 ? SelectedCapacity : 1;
        static inline constexpr size_t kBindingCapacity  = BindingCapacity > 0 ? BindingCapacity : 1;
        static inline constexpr size_t kEdgeCapacity     = EdgeCapacity > 0 ? EdgeCapacity : 1;

        size_t  selected_count = 0;                                    // 选中插件数
        size_t  binding_count  = 0;                                    // 逐需求 provider 记录数
        size_t  edge_count     = 0;                                    // 链接边数

        std::string_view selected_names[kSelectedCapacity] = {};
        RequirementBindingEvidence bindings[kBindingCapacity] = {};
        LinkEdgeEvidence           edges[kEdgeCapacity] = {};
    };

    namespace detail
    {
        // catalog 身份索引的开放寻址表容量：2 倍 catalog 上限，取 2 的幂以便用掩码取模
        inline constexpr size_t kCatalogSlotCount = 2 * kMaxCatalog;
        inline constexpr size_t kCatalogSlotMask  = kCatalogSlotCount - 1;
        static_assert((kCatalogSlotCount & kCatalogSlotMask) == 0, "catalog 槽表容量必须是 2 的幂");

        // 计划状态：排序/校验期间的可变工作区，全部定长数组，不分配堆内存
        template <size_t ActiveCapacity, size_t EdgeCapacity>
        struct PlanState
        {
            AssemblyResult<ActiveCapacity, EdgeCapacity, kMaxRequirements> result{};

            uint16_t       catalog_count = 0;
            PluginIdentity catalog_identity[kMaxCatalog] = {};
            uint16_t       active_of_catalog[kMaxCatalog] = {};
            // 身份比较键（长度 + ID + 前缀字）拆成标量数组：热路径只碰小数组，
            // 不再逐候选访问 136 字节的身份结构（常量求值对聚合访问逐条计数）
            uint32_t       catalog_key_length[kMaxCatalog] = {};
            uint64_t       catalog_key_id[kMaxCatalog]     = {};
            uint64_t       catalog_key_prefix0[kMaxCatalog] = {};
            uint64_t       catalog_key_prefix1[kMaxCatalog] = {};
            // 开放寻址：存的是 catalog 下标 + 1，0 表示空槽；查表是 O(1)，
            // 因为"每个候选一次二分查找"在常量求值里会成倍放大操作数（见 P0 证据的成本测量）
            uint16_t       catalog_slot[kCatalogSlotCount] = {};

            uint16_t       in_degree[ActiveCapacity > 0 ? ActiveCapacity : 1] = {};
            uint16_t       forward_begin[ActiveCapacity + 1] = {};
            uint16_t       forward_target[EdgeCapacity > 0 ? EdgeCapacity : 1] = {};
            bool           is_sorted[ActiveCapacity > 0 ? ActiveCapacity : 1] = {};
        };

        template <size_t ActiveCapacity, size_t EdgeCapacity>
        consteval void fail_plan(PlanState<ActiveCapacity, EdgeCapacity>& state, ErrorCode code,
                                 uint16_t plugin, uint16_t requirement, uint16_t candidate) noexcept
        {
            auto& result = state.result;
            if (result.error != ErrorCode::None)
            {
                return;
            }
            result.error             = code;
            result.error_plugin      = plugin;
            result.error_requirement = requirement;
            result.error_candidate   = candidate;
        }

        // 第一遍：收集一个 catalog 条目的元数据（不做跨条目比较）
        template <size_t ActiveCapacity, size_t EdgeCapacity, typename Declaration>
        consteval void collect_entry(PlanState<ActiveCapacity, EdgeCapacity>& state, const Declaration& declaration) noexcept
        {
            if (state.result.error != ErrorCode::None)
            {
                return;
            }
            if (state.catalog_count >= kMaxCatalog)
            {
                fail_plan(state, ErrorCode::TooManyCatalog, kNoIndex, 0, 0);
                return;
            }
            const uint16_t index = state.catalog_count;
            state.catalog_identity[index] = declaration.identity;

            ErrorCode code = declaration.error;
            if (code == ErrorCode::None && !declaration.identity.is_valid)
            {
                code = ErrorCode::InvalidName;
            }
            if (code != ErrorCode::None)
            {
                fail_plan(state, code, index,
                          static_cast<uint16_t>(declaration.error_group),
                          static_cast<uint16_t>(declaration.error_candidate));
            }
            // 容量契约必须在本阶段（任何结果索引写入之前）独立复核一遍：
            // 33 个需求组或 17 个候选即使绕过了声明期诊断，也不能进入 provider 写入
            if (state.result.error == ErrorCode::None && declaration.group_count > kMaxRequirements)
            {
                fail_plan(state, ErrorCode::TooManyRequirements, index, kMaxRequirements, 0);
            }
            if (state.result.error == ErrorCode::None)
            {
                for (uint16_t group = 0; group < declaration.group_count; ++group)
                {
                    const size_t group_candidates =
                        static_cast<size_t>(declaration.group_begin[group + 1]) - declaration.group_begin[group];
                    if (group_candidates > kMaxCandidatesPerGroup)
                    {
                        fail_plan(state, ErrorCode::TooManyCandidates, index, group, 0);
                        break;
                    }
                }
            }
            ++state.catalog_count;
        }

        template <size_t ActiveCapacity, size_t EdgeCapacity, typename Tuple, size_t... Index>
        consteval void collect_catalog(PlanState<ActiveCapacity, EdgeCapacity>& state,
                                      const Tuple& catalog, std::index_sequence<Index...>) noexcept
        {
            (collect_entry(state, std::get<Index>(catalog)), ...);
        }

        // 第二遍：catalog 内同名/碰撞检测，并建 O(1) 身份索引
        template <size_t ActiveCapacity, size_t EdgeCapacity>
        consteval void build_catalog_index(PlanState<ActiveCapacity, EdgeCapacity>& state) noexcept
        {
            if (state.result.error != ErrorCode::None)
            {
                return;
            }
            for (uint16_t i = 0; i < state.catalog_count; ++i)
            {
                const PluginIdentity& identity = state.catalog_identity[i];
                state.catalog_key_length[i]    = identity.length;
                state.catalog_key_id[i]        = identity.id;
                state.catalog_key_prefix0[i]   = identity.prefix0;
                state.catalog_key_prefix1[i]   = identity.prefix1;
                uint16_t slot                  = static_cast<uint16_t>(identity.id & kCatalogSlotMask);
                for (uint16_t probe = 0; probe < kCatalogSlotCount; ++probe)
                {
                    const uint16_t occupied = state.catalog_slot[slot];
                    if (occupied == 0)
                    {
                        state.catalog_slot[slot] = static_cast<uint16_t>(i + 1);
                        break;
                    }
                    const uint16_t existing = static_cast<uint16_t>(occupied - 1);
                    if (state.catalog_identity[existing].id == identity.id)
                    {
                        // 同 ID 时再比名字：完全同名是重名，否则是哈希碰撞，两者都不是合法身份
                        if (same_name(state.catalog_identity[existing], identity))
                        {
                            fail_plan(state, ErrorCode::DuplicatePlugin, i, 0, 0);
                        }
                        else
                        {
                            fail_plan(state, ErrorCode::NameHashCollision, i, 0, 0);
                        }
                        return;
                    }
                    slot = static_cast<uint16_t>((slot + 1) & kCatalogSlotMask);
                }
            }
        }

        // 身份 -> catalog 下标：开放寻址线性探测；同 ID 但名字不同判为碰撞
        template <size_t ActiveCapacity, size_t EdgeCapacity>
        consteval uint16_t find_catalog_by_identity(const PlanState<ActiveCapacity, EdgeCapacity>& state,
                                                    const PluginIdentity& identity, bool& collision) noexcept
        {
            collision    = false;
            uint16_t slot = static_cast<uint16_t>(identity.id & kCatalogSlotMask);
            for (uint16_t probe = 0; probe < kCatalogSlotCount; ++probe)
            {
                const uint16_t occupied = state.catalog_slot[slot];
                if (occupied == 0)
                {
                    return kNoIndex;
                }
                const uint16_t catalog_index = static_cast<uint16_t>(occupied - 1);
                if (state.catalog_identity[catalog_index].id == identity.id)
                {
                    if (same_name(state.catalog_identity[catalog_index], identity))
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

        // 第三遍：候选校验（未知候选、自依赖、组内重复、空组、名字合法）
        // 第三遍：规范化选中集合（按名字字节序排序），校验未知/重复/上限
        template <size_t ActiveCapacity, size_t EdgeCapacity, size_t SelectedCapacity>
        consteval void build_active_table(PlanState<ActiveCapacity, EdgeCapacity>& state,
                                          const std::array<PluginIdentity, SelectedCapacity>& selected) noexcept
        {
            if (state.result.error != ErrorCode::None)
            {
                return;
            }
            if (SelectedCapacity > kMaxActive)
            {
                fail_plan(state, ErrorCode::TooManyActive, kNoIndex, 0, 0);
                return;
            }
            auto& result        = state.result;
            result.active_count = static_cast<uint16_t>(SelectedCapacity);

            // provider 不再整体预填 kNoIndex：绑定阶段会为每个 active 写满 [0, active_requirement) 的槽，
            // 之后的建图/指纹/证据比对都按 active_requirement 遍历，不再把"默认 0"当成合法下标
            for (uint16_t i = 0; i < kMaxCatalog; ++i)
            {
                state.active_of_catalog[i] = kNoIndex;
            }
            for (size_t i = 0; i < SelectedCapacity; ++i)
            {
                result.active_identity[i] = selected[i];
            }
            // 插入排序：canonical 编号与 Kahn tie-break 都由名字字节序决定，与输入顺序无关
            for (size_t i = 1; i < SelectedCapacity; ++i)
            {
                const PluginIdentity key = result.active_identity[i];
                int32_t              j   = static_cast<int32_t>(i) - 1;
                while (j >= 0 && name_less(key, result.active_identity[j]))
                {
                    result.active_identity[j + 1] = result.active_identity[j];
                    --j;
                }
                result.active_identity[j + 1] = key;
            }
            for (uint16_t a = 0; a < result.active_count; ++a)
            {
                result.active_catalog_index[a] = kNoIndex;
            }
            for (uint16_t a = 0; a < result.active_count; ++a)
            {
                if (!result.active_identity[a].is_valid)
                {
                    fail_plan(state, ErrorCode::InvalidName, a, 0, 0);
                    return;
                }
                bool           collision     = false;
                const uint16_t catalog_index = find_catalog_by_identity(state, result.active_identity[a], collision);
                if (collision)
                {
                    fail_plan(state, ErrorCode::NameHashCollision, a, 0, 0);
                    return;
                }
                if (catalog_index == kNoIndex)
                {
                    fail_plan(state, ErrorCode::UnknownActive, a, 0, 0);
                    return;
                }
                for (uint16_t other = 0; other < a; ++other)
                {
                    if (result.active_catalog_index[other] == catalog_index)
                    {
                        fail_plan(state, ErrorCode::DuplicateActive, a, 0, 0);
                        return;
                    }
                }
                result.active_catalog_index[a]        = catalog_index;
                state.active_of_catalog[catalog_index] = a;
            }
        }

        // 第四遍：候选引用校验 + 逐需求唯一绑定（一次遍历完成两件事）
        //   每个候选只做一次 O(1) 身份查表；provider 先算进本插件的临时行，行内全部合法后才写结果，
        //   因此"校验先于结果索引写"在单个插件范围内成立，容量契约已在前一遍复核。
        template <size_t ActiveCapacity, size_t EdgeCapacity, typename Declaration>
        consteval void bind_entry(PlanState<ActiveCapacity, EdgeCapacity>& state, uint16_t catalog_index,
                                  const Declaration& declaration) noexcept
        {
            if (state.result.error != ErrorCode::None)
            {
                return;
            }
            constexpr size_t kSeenWords   = (kMaxCatalog + 31) / 32;
            auto&            result       = state.result;
            const uint16_t   active       = state.active_of_catalog[catalog_index];
            const bool       is_selected  = active != kNoIndex;

            // 每个选定插件的绑定先落在临时行里：只有该插件的全部需求都通过，才写进结果索引
            uint16_t row[kMaxRequirements] = {};

            for (uint16_t group = 0; group < declaration.group_count; ++group)
            {
                const uint16_t begin = declaration.group_begin[group];
                const uint16_t end   = declaration.group_begin[group + 1];
                if (begin == end)
                {
                    fail_plan(state, ErrorCode::EmptyRequirementGroup, catalog_index, group, 0);
                    return;
                }
                uint32_t seen[kSeenWords] = {};
                uint16_t matches          = 0;
                uint16_t provider         = kNoIndex;
                for (uint16_t candidate = begin; candidate < end; ++candidate)
                {
                    // 紧凑候选键：只从标量数组读 5 个标量与池内名字字节，不复制 144 字节身份结构
                    const uint32_t length  = declaration.candidate_length[candidate];
                    const uint64_t id      = declaration.candidate_id[candidate];
                    const uint64_t prefix0 = declaration.candidate_prefix0[candidate];
                    const uint64_t prefix1 = declaration.candidate_prefix1[candidate];
                    const char*    bytes   = declaration.candidate_pool + declaration.candidate_offset[candidate];
                    if (length == 0)
                    {
                        fail_plan(state, ErrorCode::InvalidName, catalog_index, group, candidate);
                        return;
                    }
                    // 自依赖：必须完整名字相等；只比 (长度, ID, 前缀) 会把真实哈希碰撞误判成自依赖
                    if (name_equals(declaration.identity, bytes, length, id, prefix0, prefix1))
                    {
                        fail_plan(state, ErrorCode::SelfDependency, catalog_index, group, candidate);
                        return;
                    }
                    // 身份探测内联：每个候选只有一次哈希 + 一次标量键比较，
                    // 不再跨越多次 consteval 调用（常量求值对函数调用与循环逐条计数）
                    uint16_t  slot   = static_cast<uint16_t>(id & kCatalogSlotMask);
                    uint16_t  found  = kNoIndex;
                    ErrorCode lookup = ErrorCode::None;
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
                            if (state.catalog_key_length[hit] == length &&
                                state.catalog_key_prefix0[hit] == prefix0 &&
                                state.catalog_key_prefix1[hit] == prefix1 &&
                                (length <= 16 || name_equals(state.catalog_identity[hit], bytes, length, id, prefix0, prefix1)))
                            {
                                found = hit;
                            }
                            else
                            {
                                lookup = ErrorCode::NameHashCollision;
                            }
                            break;
                        }
                        slot = static_cast<uint16_t>((slot + 1) & kCatalogSlotMask);
                    }
                    if (lookup != ErrorCode::None)
                    {
                        fail_plan(state, lookup, catalog_index, group, candidate);
                        return;
                    }
                    if (found == kNoIndex)
                    {
                        fail_plan(state, ErrorCode::UnknownCandidate, catalog_index, group, candidate);
                        return;
                    }
                    // 组内重复用 catalog 下标位图，避免每个候选再扫一遍同组前缀（O(k^2) -> O(1)）
                    const uint32_t word = static_cast<uint32_t>(found) >> 5;
                    const uint32_t bit  = 1u << (static_cast<uint32_t>(found) & 31u);
                    if ((seen[word] & bit) != 0)
                    {
                        fail_plan(state, ErrorCode::DuplicateCandidate, catalog_index, group, candidate);
                        return;
                    }
                    seen[word] |= bit;
                    if (is_selected)
                    {
                        const uint16_t provider_active = state.active_of_catalog[found];
                        if (provider_active != kNoIndex)
                        {
                            ++matches;
                            provider = provider_active;
                            if (matches > 1)
                            {
                                break;
                            }
                        }
                    }
                }
                if (is_selected)
                {
                    if (matches == 0)
                    {
                        fail_plan(state, ErrorCode::MissingProvider, active, group, 0);
                        return;
                    }
                    if (matches > 1)
                    {
                        fail_plan(state, ErrorCode::AmbiguousProvider, active, group, 0);
                        return;
                    }
                    row[group] = provider;
                }
            }

            if (is_selected)
            {
                for (uint16_t group = 0; group < declaration.group_count; ++group)
                {
                    result.provider[static_cast<size_t>(active) * kMaxRequirements + group] = row[group];
                }
                // 记录本插件的实际需求组数：后续建图、指纹与证据比对只遍历这一段，
                // 不再对每个 active 固定扫 32 个槽（未使用的槽没有意义）。
                result.active_requirement[active] = declaration.group_count;
            }
        }

        template <size_t ActiveCapacity, size_t EdgeCapacity, typename Tuple, size_t... Index>
        consteval void bind_catalog(PlanState<ActiveCapacity, EdgeCapacity>& state,
                                   const Tuple& catalog, std::index_sequence<Index...>) noexcept
        {
            uint16_t cursor = 0;
            ((bind_entry(state, static_cast<uint16_t>(cursor++), std::get<Index>(catalog))), ...);
        }

        // 第六遍：去重边、入度、确定性 Kahn 拓扑序、环诊断、指纹
        template <size_t ActiveCapacity, size_t EdgeCapacity>
        consteval void build_graph(PlanState<ActiveCapacity, EdgeCapacity>& state) noexcept
        {
            if (state.result.error != ErrorCode::None)
            {
                return;
            }
            auto&    result      = state.result;
            const size_t count   = result.active_count;

            // 边按下游分组存放：同一上游满足多个需求时只保留一条边（计划书 6.3）
            // 只遍历该 active 的实际需求组数（active_requirement），不按每 active 固定 32 扫
            for (uint16_t dependent = 0; dependent < count; ++dependent)
            {
                result.edge_begin[dependent] = result.edge_count;
                const uint16_t requirement_count = result.active_requirement[dependent];
                for (uint16_t requirement = 0; requirement < requirement_count; ++requirement)
                {
                    const uint16_t provider = result.provider[static_cast<size_t>(dependent) * kMaxRequirements + requirement];
                    bool duplicate = false;
                    for (uint16_t previous = 0; previous < requirement; ++previous)
                    {
                        if (result.provider[static_cast<size_t>(dependent) * kMaxRequirements + previous] == provider)
                        {
                            duplicate = true;
                            break;
                        }
                    }
                    if (duplicate)
                    {
                        continue;
                    }
                    if (result.edge_count >= EdgeCapacity)
                    {
                        fail_plan(state, ErrorCode::InvalidDeclaration, dependent, requirement, 0);
                        return;
                    }
                    result.edge_target[result.edge_count] = provider;
                    ++result.edge_count;
                    ++state.in_degree[dependent];
                }
            }
            result.edge_begin[count] = result.edge_count;

            // 正向邻接（provider -> dependent）用于 Kahn 减入度
            for (uint16_t i = 0; i <= count; ++i)
            {
                state.forward_begin[i] = 0;
            }
            for (uint16_t dependent = 0; dependent < count; ++dependent)
            {
                for (uint16_t e = result.edge_begin[dependent]; e < result.edge_begin[dependent + 1]; ++e)
                {
                    ++state.forward_begin[result.edge_target[e] + 1];
                }
            }
            for (uint16_t i = 0; i < count; ++i)
            {
                state.forward_begin[i + 1] += state.forward_begin[i];
            }
            {
                uint16_t write_cursor[ActiveCapacity > 0 ? ActiveCapacity : 1] = {};
                for (uint16_t i = 0; i < count; ++i)
                {
                    write_cursor[i] = state.forward_begin[i];
                }
                for (uint16_t dependent = 0; dependent < count; ++dependent)
                {
                    for (uint16_t e = result.edge_begin[dependent]; e < result.edge_begin[dependent + 1]; ++e)
                    {
                        const uint16_t provider = result.edge_target[e];
                        state.forward_target[write_cursor[provider]++] = dependent;
                    }
                }
            }

            // Kahn：canonical 下标本身按名字字节序编号，取第一个就绪节点即"最小名字"
            uint16_t order_count = 0;
            while (order_count < count)
            {
                int32_t pick = -1;
                for (uint16_t a = 0; a < count; ++a)
                {
                    if (!state.is_sorted[a] && state.in_degree[a] == 0)
                    {
                        pick = static_cast<int32_t>(a);
                        break;
                    }
                }
                if (pick < 0)
                {
                    break;
                }
                state.is_sorted[pick]            = true;
                result.topo_order[order_count++] = static_cast<uint16_t>(pick);
                for (uint16_t e = state.forward_begin[pick]; e < state.forward_begin[pick + 1]; ++e)
                {
                    const uint16_t dependent = state.forward_target[e];
                    --state.in_degree[dependent];
                }
            }

            if (order_count != static_cast<uint16_t>(count))
            {
                fail_plan(state, ErrorCode::Cycle, result.error_plugin, 0, 0);
                // 真实环提取（容量 = active 上限，不截断）：
                //   沿"下游 -> 上游"游走。任何未排序节点都至少有一个未排序上游——它从未把入度减到 0，
                //   而能减到 0 的前提是全部上游都已排序。因此从任意未排序起点出发，第一次遇到重复节点
                //   的位置就是一个真实环的入口；丢掉环之前的残余前缀，剩下的成员即环本身。
                //   error_nodes[i+1] 是 error_nodes[i] 的一个上游，末尾回到 error_nodes[0]。
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
                        uint16_t previous = kNoIndex;
                        for (uint16_t e = result.edge_begin[node]; e < result.edge_begin[node + 1]; ++e)
                        {
                            if (!state.is_sorted[result.edge_target[e]])
                            {
                                previous = result.edge_target[e];
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

            // 指纹：canonical 名字序列 + 拓扑序 + 每个需求的 provider
            uint64_t hash = 14695981039346656037ull;
            for (uint16_t a = 0; a < result.active_count; ++a)
            {
                for (uint32_t i = 0; i < result.active_identity[a].length; ++i)
                {
                    hash = hash_mix(hash, static_cast<unsigned char>(result.active_identity[a].name[i]));
                }
                hash = hash_mix(hash, 0xFFu);
            }
            for (uint16_t i = 0; i < result.active_count; ++i)
            {
                hash = hash_mix(hash, result.topo_order[i]);
            }
            for (uint16_t a = 0; a < result.active_count; ++a)
            {
                const uint16_t requirement_count = result.active_requirement[a];
                for (uint16_t requirement = 0; requirement < requirement_count; ++requirement)
                {
                    hash = hash_mix(hash, result.provider[static_cast<size_t>(a) * kMaxRequirements + requirement]);
                }
            }
            result.fingerprint = hash;
        }

        template <typename Result>
        consteval uint16_t active_index_of(const Result& plan, std::string_view name, bool& found) noexcept
        {
            found = false;
            for (uint16_t a = 0; a < plan.active_count; ++a)
            {
                if (plan.active_identity[a].view() == name)
                {
                    found = true;
                    return a;
                }
            }
            return kNoIndex;
        }
    } // namespace detail

    /**
     *  @brief 在组装结果里按插件名查找 canonical active 下标
     *  @param plan sort_and_validate 的结果
     *  @param name 插件名
     *  @return canonical 下标；找不到返回 kNoIndex
     *  @note 只读查询助手（供生成的校验 TU 与测试使用），不是第四个构建入口
     */
    template <typename Result>
    constexpr uint16_t find_active_index(const Result& plan, std::string_view name) noexcept
    {
        for (uint16_t index = 0; index < plan.active_count; ++index)
        {
            if (plan.active_identity[index].view() == name)
            {
                return index;
            }
        }
        return kNoIndex;
    }

    /**
     *  @brief 第三个操作族：绑定、建图、拓扑排序与结构化校验（计划书 5.4 / 6）
     *  @param catalog 完整 plugin catalog（tuple of PluginDeclaration），条目上限 256
     *  @param selected 本产品显式选中的插件身份数组；空数组（N=0）是合法的零插件产品
     *  @return AssemblyResult，成功时含规范化 active 表、拓扑序、逐需求 provider、去重边与指纹
     *  @note 纯函数：不写任何全局状态；不因为输入顺序不同而改变绑定或顺序
     */
    template <typename Catalog, size_t SelectedCapacity>
    consteval auto sort_and_validate(const Catalog& catalog,
                                     const std::array<PluginIdentity, SelectedCapacity>& selected) noexcept
        -> AssemblyResult<SelectedCapacity, SelectedCapacity * kMaxRequirements, kMaxRequirements>
    {
        using Result = AssemblyResult<SelectedCapacity, SelectedCapacity * kMaxRequirements, kMaxRequirements>;
        constexpr size_t kCatalogSize = std::tuple_size_v<std::remove_cvref_t<Catalog>>;

        detail::PlanState<SelectedCapacity, SelectedCapacity * kMaxRequirements> state{};
        Result result{};

        if constexpr (kCatalogSize > kMaxCatalog)
        {
            result.error = ErrorCode::TooManyCatalog;
            return result;
        }
        else
        {
            detail::collect_catalog(state, catalog, std::make_index_sequence<kCatalogSize>{});
            detail::build_catalog_index(state);
            detail::build_active_table(state, selected);
            // 候选引用校验与唯一绑定在同一次遍历里完成：每个候选只查一次 O(1) 身份表
            detail::bind_catalog(state, catalog, std::make_index_sequence<kCatalogSize>{});
            detail::build_graph(state);
            return state.result;
        }
    }

    /**
     *  @brief 用构建系统派生的证据交叉校验 C++ 推导结果（计划书 8.5）
     *  @param plan sort_and_validate 的结果
     *  @param evidence CMake 从同一份 JSON 派生的选中集合 / 逐需求 provider / 真实链接边
     *  @return None 表示逐项一致；否则返回具体的不一致类别
     *  @note 三类证据都做**双向一一匹配**：每条证据必须消费一个尚未被消费的计划条目，
     *        因此重复记录（如 selected{Alpha,Alpha} 代替 {Alpha,Beta}、两条相同 provider 记录
     *        代替两条不同需求、两条相同边代替另一条边）必然被判为不一致，不能被数量相等掩盖。
     *        所有下标都在索引之前完成容量与范围检查，越界计数只返回类别、不读数组。
     *        顺序仍是"选中集合 -> 逐需求绑定 -> 去重边"；只做常量求值，无运行期成本。
     */
    template <typename Result, typename Evidence>
    consteval ErrorCode compare_evidence(const Result& plan, const Evidence& evidence) noexcept
    {
        if (plan.error != ErrorCode::None)
        {
            return plan.error;
        }

        // ---- 1. 选中集合：每条证据名字消费一个不同的 active 下标 ----
        if (evidence.selected_count != plan.active_count ||
            evidence.selected_count > Evidence::kSelectedCapacity)
        {
            return ErrorCode::EvidenceSelectionMismatch;
        }
        bool consumed_active[Result::kActiveCapacity > 0 ? Result::kActiveCapacity : 1] = {};
        for (size_t i = 0; i < evidence.selected_count; ++i)
        {
            bool           found  = false;
            const uint16_t active = detail::active_index_of(plan, evidence.selected_names[i], found);
            if (!found || active >= plan.active_count || consumed_active[active])
            {
                return ErrorCode::EvidenceSelectionMismatch;
            }
            consumed_active[active] = true;
        }

        // ---- 2. 逐需求 provider：条数等于计划的实际绑定数，且每条消费不同的 (active, requirement) ----
        size_t expected_bindings = 0;
        for (uint16_t a = 0; a < plan.active_count; ++a)
        {
            expected_bindings += plan.active_requirement[a];
        }
        if (evidence.binding_count != expected_bindings || evidence.binding_count > Evidence::kBindingCapacity)
        {
            return ErrorCode::EvidenceProviderMismatch;
        }
        constexpr size_t kBindingSlots = Result::kActiveCapacity * Result::kRequirementCapacity;
        bool             consumed_binding[kBindingSlots > 0 ? kBindingSlots : 1] = {};
        for (size_t i = 0; i < evidence.binding_count; ++i)
        {
            const auto&    record = evidence.bindings[i];
            bool           found  = false;
            const uint16_t active = detail::active_index_of(plan, record.dependent, found);
            if (!found || active >= plan.active_count)
            {
                return ErrorCode::EvidenceProviderMismatch;
            }
            if (record.requirement >= plan.active_requirement[active])
            {
                return ErrorCode::EvidenceProviderMismatch;
            }
            const size_t   slot     = static_cast<size_t>(active) * Result::kRequirementCapacity + record.requirement;
            const uint16_t provider = plan.provider[slot];
            if (provider >= plan.active_count || plan.active_identity[provider].view() != record.provider)
            {
                return ErrorCode::EvidenceProviderMismatch;
            }
            if (consumed_binding[slot])
            {
                return ErrorCode::EvidenceProviderMismatch;
            }
            consumed_binding[slot] = true;
        }

        // ---- 3. 链接边：条数等于计划的去重边数，且每条消费一条尚未被消费的边 ----
        if (evidence.edge_count != plan.edge_count || evidence.edge_count > Evidence::kEdgeCapacity)
        {
            return ErrorCode::EvidenceEdgeMismatch;
        }
        bool consumed_edge[Result::kEdgeCapacity > 0 ? Result::kEdgeCapacity : 1] = {};
        for (size_t i = 0; i < evidence.edge_count; ++i)
        {
            const auto&    record          = evidence.edges[i];
            bool           found_dependent = false;
            const uint16_t dependent       = detail::active_index_of(plan, record.to, found_dependent);
            if (!found_dependent || dependent >= plan.active_count)
            {
                return ErrorCode::EvidenceEdgeMismatch;
            }
            bool matched = false;
            for (uint16_t e = plan.edge_begin[dependent]; e < plan.edge_begin[dependent + 1]; ++e)
            {
                if (!consumed_edge[e] && plan.active_identity[plan.edge_target[e]].view() == record.from)
                {
                    consumed_edge[e] = true;
                    matched          = true;
                    break;
                }
            }
            if (!matched)
            {
                return ErrorCode::EvidenceEdgeMismatch;
            }
        }
        return ErrorCode::None;
    }
} // namespace ID::Bench::Fixture
