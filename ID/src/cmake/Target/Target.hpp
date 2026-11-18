#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "cmake/Target/Scope.hpp"
#include "cmake/Target/TargetType.hpp"
#include "cmake/Value/Argument.hpp"

/**
 *  目标层：单个 CMake 目标的字段模型（多目录收源、编译标准、单目录与按配置输出目录、形态选项、
 *  目标守卫、互斥分支、BUILD_RPATH、POSITION_INDEPENDENT_CODE、import 规格）
 *  与四组带作用域的语句条目（编译宏、include、link 目录、link 库）。
 *
 *  职责边界：只存数据并提供只读面，不产文本、不做唯一性校验、不解析链接目标名。文本由
 *  emit_target 产出（骨架顺序与排版归它单方决定，模型不存语句序号）；跨目标、跨目录与
 *  全工程的校验由 validate(const Project&) 判定。只读面覆盖 emit_target 与 validate 需要读的
 *  全部字段，两者都不靠友元、也不需要实现方再补接口。
 *
 *  所有权：对象只能由 Directory::create_target 构造并入容器，外部不得构造游离对象，因此构造函数
 *  私有且 Directory 是友元；拷贝被删除，引用与视图指向按值存放的成员，有效期到下一次对该目标的
 *  set_* / add_* 调用之前，用时现取。
 *
 *  离线工具、非运行时热路径：本模块不在逐帧或逐元素路径上，std::string / std::vector 的分配成本可接受，
 *  模型层因此不做逐点分配失败检查——这是对 cpp-style 第 9 节「分配失败不得忽略」的显式例外
 *  （依据：CMake 封装计划书第 1.2 节第 10 项，范围仅限容器与字符串分配）。
 */

namespace ID::CMake
{
    class Directory;

    // 收源目录的默认取值：根 plugin-targets 片段按它给出首个目录；validate 不再据它判非法
    // （清单声明 source_dirs 多于或异于 ["src"] 时由生成侧按声明逐项登记）
    inline constexpr std::string_view kSourceGlobDirectory = "src";

    // 首版唯一支持的编译特性；其它取值由 validate 报 UnsupportedCompileStandard
    inline constexpr std::string_view kSupportedCompileStandard = "cxx_std_20";

    /**
     *  条目类别：四组语句条目各自独立的容器维度。各项显式赋值，末尾 Count 只作容器维度与
     *  循环上界，不传给 entry_count / entry_at（references/design-patterns.md 第 3 节）。
     */
    enum class TargetEntryKind : std::uint8_t
    {
        CompileDefinition = 0,   // target_compile_definitions
        IncludeDirectory  = 1,   // target_include_directories，可带 SYSTEM
        LinkDirectory     = 2,   // target_link_directories
        LinkLibrary       = 3,   // target_link_libraries

        Count             = 4,   // 哨兵：不是合法类别，只作容器维度与循环上界
    };

    static_assert(sizeof(TargetEntryKind) == sizeof(std::uint8_t),
                  "TargetEntryKind 的底层类型必须与声明一致，否则容器维度与序号转换失效");

    constexpr std::uint8_t to_target_entry_kind_uint(TargetEntryKind kind) noexcept
    {
        return static_cast<std::uint8_t>(kind);
    }

    constexpr TargetEntryKind to_target_entry_kind(std::uint8_t kind) noexcept
    {
        return static_cast<TargetEntryKind>(kind);
    }

    /**
     *  库形态选项：由 option 选择 STATIC / SHARED 的目标需要的三个取值。
     *  数据载体不带判定：目标类型是否可切形态、option 是否在同目录声明过，都由 validate 判定
     *  （两类触发都归 InvalidFormOptionTarget）。
     */
    struct FormOption
    {
        Argument option;          // 库形态选项名，形如 IDWINDOW_BUILD_SHARED
        Argument shared_define;   // SHARED 分支的导出宏
        Argument static_define;   // STATIC 分支的静态宏
    };

    /**
     *  IMPORTED 目标的产物登记。合法性（哪些类型允许哪些字段、哪些类型禁止出现）由 validate 判定
     *  （MissingImportLocation / ImportTargetSetting），set_import 只登记取值。
     */
    struct ImportSpec
    {
        std::optional<Argument> location;      // IMPORTED_LOCATION
        std::optional<Argument> implib;        // IMPORTED_IMPLIB，只允许 SharedLibrary 给出
        bool                    is_global = false;   // 为真时第 6 项带 GLOBAL
    };

    /**
     *  一条带作用域的语句条目。空 optional 的 condition 表示无条件：判定只用 condition.has_value()，
     *  空文本不表示无条件（Quoted / QuotedExpand 允许空串）。
     */
    struct ScopedEntry
    {
        Scope                   scope;                // 作用域关键字
        Argument                value;                // 参数取值
        std::optional<Argument> condition;            // 空 optional 表示无条件
        bool                    is_system = false;    // 只对 IncludeDirectory 有意义
    };

    /**
     *  按配置的输出目录：单一配置生成器下写死 debug / release 两个位置。
     *  与既有的 set_output_directory 互斥（同一目标只能选其中一种表达，由 validate 判 InvalidConfigOutput）。
     *  取值只拼配置段（形如 ${ID_OUTPUT_ROOT}/debug），不展开 toolchain/arch 身份段、不自带 fallback。
     */
    struct ConfigOutputDirectories
    {
        Argument debug;      // debug 配置的输出目录
        Argument release;    // release 配置的输出目录
    };

    /**
     *  目标创建前的拒绝守卫：`if(TARGET <name>) message(FATAL_ERROR ...) endif()`。
     *  形态固定为拒绝，不是静默跳过；空目标名、非 Bare 目标名与含 CR/LF 的说明串由 validate 判 InvalidGuard。
     *  目标块（第 5 与第 6 项之间）与子目录接入（第 13 项）共用本结构，只此一份守卫排版。
     */
    struct TargetGuard
    {
        Argument target;     // 被守卫的目标名，必须是非空 Bare
        Argument message;    // FATAL_ERROR 的说明串，通常为 Quoted
    };

    /**
     *  互斥分支块的载荷类别：两分支必须是同一条命令的同一种载荷（由 validate 判 InvalidMacroBranch）。
     */
    enum class BranchPayload : std::uint8_t
    {
        CompileDefinition = 0,   // target_compile_definitions(<target> <scope> <value>)
        Variable          = 1,   // set(<value>)：value 承载 set 的完整参数串（变量名与取值），按 Verbatim 承载
    };

    static_assert(sizeof(BranchPayload) == sizeof(std::uint8_t),
                  "BranchPayload 的底层类型必须与声明一致，否则按值传递与诊断取值约定失效");

    /**
     *  分支块的一个分支：载荷类别、作用域（只对 CompileDefinition 有意义）与取值。数据载体不带判定。
     */
    struct BranchStatement
    {
        BranchPayload payload = BranchPayload::CompileDefinition;   // 载荷类别
        Scope         scope   = Scope::Private;                     // 仅 CompileDefinition 使用
        Argument      value   = Argument::quoted("");               // 宏定义或变量取值（空引号是占位，不是合法取值）
    };

    /**
     *  一个 if()/else() 互斥分支块：条件、两个分支。条件由调用方给出，可为单一选项名或原始表达式；
     *  模型不解析布尔表达式、不做 OR 合并。
     */
    struct BranchBlock
    {
        Argument        condition      = Argument::quoted("");   // if(<condition>) 的条件（空引号是占位，不是合法条件）
        BranchStatement then_statement = { };                   // if 分支
        BranchStatement else_statement = { };                   // else 分支
    };

    /**
     *  @brief 目标名的大写折叠形式
     *  @param name 目标名
     *  @return 只折叠 ASCII 小写字母得到的字符串，不依赖 locale
     *  @note 派生变量名 `<TARGET>_SRC_FILES` / `<TARGET>_LIBRARY_TYPE` 的生成与
     *        DuplicateDerivedVariable 判重共用这一处定义，避免两处折叠规则不一致
     */
    std::string to_upper_name(std::string_view name);

    /**
     *  单个 CMake 目标。实例只能由 Directory::create_target 创建。
     */
    class Target
    {
    public:
        Target(const Target&)            = delete;
        Target& operator=(const Target&) = delete;

        /**
         *  @brief 目标名，CMake 里的唯一键
         */
        std::string_view get_name() const noexcept;

        /**
         *  @brief 目标类型；可能仍是哨兵 None，由 validate 报 InvalidTargetType
         */
        TargetType get_type() const noexcept;

        /**
         *  @brief 首条源通配目录（既有单目录调用点的只读面）
         *  @return 一条都没有时为空串；多条时只给第一条，全量读取用 source_glob_dir_count / source_glob_dir_at
         *  @note 与 has_source_glob_dir 成对使用：只看取值无法区分"未登记"与"登记了空条目"
         */
        std::string_view get_source_glob_dir() const noexcept;

        /**
         *  @brief 是否登记过至少一条源通配目录
         */
        bool has_source_glob_dir() const noexcept;

        /**
         *  @brief 源通配目录的条数
         */
        std::size_t source_glob_dir_count() const noexcept;

        /**
         *  @brief 取一条源通配目录
         *  @param index 下标，必须小于 source_glob_dir_count
         *  @return 指向本对象内部存储的视图，有效期到下一次对该目标的 add_source_glob_dir / set_source_glob_dir 之前
         *  @note 下标越界属调用方违约，用 ID_ASSERT 终止，不进诊断通道
         */
        std::string_view source_glob_dir_at(std::size_t index) const noexcept;

        /**
         *  @brief 是否调用过 set_compile_standard
         *  @note 未调用时编译特性按目标类型的自动展开规则写出；INTERFACE 库与 imported 目标调用它是
         *        设置项误用（InterfaceTargetSetting / ImportTargetSetting），因此必须能区分"未调用"
         *        与"调用但取值合法"，不能只看取值
         */
        bool has_compile_standard() const noexcept;

        /**
         *  @brief 编译特性取值
         *  @return 未调用 set_compile_standard 时返回自持的默认 cxx_std_20 成员引用，不是临时对象
         */
        const Argument& get_compile_standard() const noexcept;

        /**
         *  @brief 是否调用过 set_output_directory
         */
        bool has_output_directory() const noexcept;

        /**
         *  @brief 输出目录取值
         *  @return 未调用 set_output_directory 时返回自持的空引号片段引用，不是临时对象；
         *          调用方必须先查 has_output_directory，未设置时该取值不参与输出
         */
        const Argument& get_output_directory() const noexcept;

        /**
         *  @brief 是否调用过 set_form_option
         */
        bool has_form_option() const noexcept;

        /**
         *  @brief 库形态选项的三个取值
         *  @return 未调用 set_form_option 时返回自持的空片段引用，不是临时对象；
         *          调用方必须先查 has_form_option
         */
        const FormOption& get_form_option() const noexcept;

        /**
         *  @brief 是否调用过 set_import（即是否为 imported 目标）
         */
        bool has_import() const noexcept;

        /**
         *  @brief IMPORTED 目标的产物登记
         *  @return has_import 为假时返回自持的空 ImportSpec 成员引用，不是临时对象
         */
        const ImportSpec& get_import() const noexcept;

        /**
         *  @brief 是否调用过 set_config_output_directories
         */
        bool has_config_output_directories() const noexcept;

        /**
         *  @brief 按配置的输出目录
         *  @return 未调用时返回自持的空片段取值引用，不是临时对象；调用方必须先查 has_config_output_directories
         */
        const ConfigOutputDirectories& get_config_output_directories() const noexcept;

        /**
         *  @brief 是否调用过 set_guard
         */
        bool has_guard() const noexcept;

        /**
         *  @brief 目标守卫
         *  @return 未调用时返回自持的空片段取值引用，不是临时对象；调用方必须先查 has_guard
         */
        const TargetGuard& get_guard() const noexcept;

        /**
         *  @brief 互斥分支块的数量
         */
        std::size_t branch_count() const noexcept;

        /**
         *  @brief 取一个互斥分支块
         *  @param index 下标，必须小于 branch_count
         *  @return 容器内元素的引用，有效期到下一次对该目标的 add_branch 之前
         *  @note 下标越界属调用方违约，用 ID_ASSERT 终止，不进诊断通道
         */
        const BranchBlock& branch_at(std::size_t index) const noexcept;

        /**
         *  @brief 是否调用过 set_build_rpath
         */
        bool has_build_rpath() const noexcept;

        /**
         *  @brief 构建期运行搜索路径，按登记顺序保序
         *  @return 未调用时返回自持的空容器引用，不是临时对象
         */
        const std::vector<std::string>& get_build_rpath() const noexcept;

        /**
         *  @brief 是否调用过 set_position_independent_code
         *  @note 取值只有 ON / OFF 两种合法状态，因此本函数是区分"未调用"与"调用并取 false"的唯一依据
         */
        bool has_position_independent_code() const noexcept;

        /**
         *  @brief POSITION_INDEPENDENT_CODE 的取值
         *  @return 未调用时为 false；调用方必须先查 has_position_independent_code
         */
        bool get_position_independent_code() const noexcept;

        /**
         *  @brief 某类条目的数量
         *  @param kind 条目类别；不得传 TargetEntryKind::Count
         */
        std::size_t entry_count(TargetEntryKind kind) const noexcept;

        /**
         *  @brief 取某类条目中的一条
         *  @param kind 条目类别；不得传 TargetEntryKind::Count
         *  @param index 下标，必须小于同 kind 的 entry_count
         *  @return 容器内元素的引用，有效期到下一次对该目标的 add_* / set_* 调用之前
         *  @note 下标越界属调用方违约，用 ID_ASSERT 终止，不进诊断通道
         */
        const ScopedEntry& entry_at(TargetEntryKind kind, std::size_t index) const noexcept;

        /**
         *  @brief 追加一条源通配目录，可多次调用并按调用顺序保序
         *  @param relative_dir 相对插件根的目录；绝对路径（含盘符或前导分隔符）、含 .. 段、越出插件根、
         *                      空串或只含分隔符的条目由 validate 报 InvalidSourceGlobDir，本方法只登记
         *  @note 条目落在清单 source_dirs 声明的范围内由装配层保证，本层不读清单、不限制目录名
         */
        void add_source_glob_dir(std::string_view relative_dir);

        /**
         *  @brief 设置唯一的源通配目录：清空已登记的条目后只加一条（既有单目录调用点的兼容包装）
         *  @param relative_dir 同 add_source_glob_dir
         */
        void set_source_glob_dir(std::string_view relative_dir);

        /**
         *  @brief 设置编译特性
         *  @param feature 只接受 cxx_std_20，其它取值由 validate 报 UnsupportedCompileStandard
         */
        void set_compile_standard(Argument feature);

        /**
         *  @brief 设置输出目录
         *  @param directory 目录取值，通常为 ${CMAKE_SOURCE_DIR}/bin 之类的可重定位表达
         *  @note 属性展开由 emitter 决定：形态可切换目标三项全写，其它按目标类型展开；
         *        INTERFACE 库与 imported 目标调用本方法由 validate 报设置项误用
         */
        void set_output_directory(Argument directory);

        /**
         *  @brief 追加一条编译宏条目
         *  @param scope 作用域
         *  @param definition 宏定义取值
         *  @param condition 可选条件；空 optional 表示无条件
         */
        void add_compile_definition(Scope scope, Argument definition, std::optional<Argument> condition = std::nullopt);

        /**
         *  @brief 追加一条 include 目录条目
         *  @param scope 作用域
         *  @param path include 路径
         *  @param condition 可选条件；空 optional 表示无条件
         *  @param is_system 为真时以 SYSTEM 关键字输出（SYSTEM 写在作用域关键字之前）
         */
        void add_include_directory(Scope scope, Argument path, std::optional<Argument> condition = std::nullopt, bool is_system = false);

        /**
         *  @brief 追加一条链接目录条目
         */
        void add_link_directory(Scope scope, Argument path, std::optional<Argument> condition = std::nullopt);

        /**
         *  @brief 追加一条链接库条目
         *  @param library 目标名；解析范围是全工程（已声明目标名 + Project::add_external_target 登记）
         */
        void add_link_library(Scope scope, Argument library, std::optional<Argument> condition = std::nullopt);

        /**
         *  @brief 设置库形态选项
         *  @note 只允许 StaticLibrary / SharedLibrary；其它类型与"option 未在同目录声明"都由 validate
         *        报 InvalidFormOptionTarget
         */
        void set_form_option(Argument option, Argument shared_define, Argument static_define);

        /**
         *  @brief 登记 IMPORTED 目标的产物
         *  @param location IMPORTED_LOCATION；InterfaceLibrary 必须为空，其余类型必填
         *  @param implib IMPORTED_IMPLIB；只允许 SharedLibrary 给出
         *  @param is_global 为真时第 6 项带 GLOBAL
         *  @note 只登记取值，合法性由 validate 判定（MissingImportLocation / ImportTargetSetting）
         */
        void set_import(std::optional<Argument> location, std::optional<Argument> implib, bool is_global = false);

        /**
         *  @brief 登记按配置的输出目录（第 9 项写出 *_OUTPUT_DIRECTORY_DEBUG / _RELEASE）
         *  @param debug_directory debug 配置的输出目录取值，形如 ${ID_OUTPUT_ROOT}/debug
         *  @param release_directory release 配置的输出目录取值，形如 ${ID_OUTPUT_ROOT}/release
         *  @note 与 set_output_directory 互斥，两个都登记由 validate 报 InvalidConfigOutput；
         *        空取值同属 InvalidConfigOutput，本方法只登记
         */
        void set_config_output_directories(Argument debug_directory, Argument release_directory);

        /**
         *  @brief 登记目标创建前的拒绝守卫（第 5 与第 6 项之间写出）
         *  @param target 被守卫的目标名，必须是非空 Bare
         *  @param message message(FATAL_ERROR ...) 的说明串
         *  @note 空目标名、非 Bare 目标名或含 CR/LF 的说明串由 validate 报 InvalidGuard
         */
        void set_guard(Argument target, Argument message);

        /**
         *  @brief 追加一个互斥 if()/else() 分支块（第 7 项，在无条件编译宏之后按插入顺序写出）
         *  @param condition 条件：单一选项名或调用方给出的原始表达式，模型不做布尔求解
         *  @param then_statement if 分支
         *  @param else_statement else 分支
         *  @note 条件为空、两分支载荷类别不同或分支取值为空由 validate 报 InvalidMacroBranch
         */
        void add_branch(Argument condition, BranchStatement then_statement, BranchStatement else_statement);

        /**
         *  @brief 登记构建期运行搜索路径（第 9 项写出 BUILD_RPATH）
         *  @param directories 字面目录值，按登记顺序保序；写出时以 ';' 连接为一个引号字符串
         *  @note 空列表、含空串的项或含 $<CONFIG> 的取值由 validate 报 InvalidBuildRpath；
         *        不写 INSTALL_RPATH、不写全局变量
         */
        void set_build_rpath(std::vector<std::string> directories);

        /**
         *  @brief 登记是否写出 POSITION_INDEPENDENT_CODE（第 9 项，与输出目录、CXX_EXTENSIONS 并列）
         *  @param enabled 为真写 ON，否则写 OFF
         *  @note 判定依据由装配层给出（静态库是否会被链接进某个 SHARED 目标），模型不推导链接关系；
         *        INTERFACE 库与 imported 目标调用本项由 validate 报设置项误用
         */
        void set_position_independent_code(bool enabled);

    private:
        friend class Directory;

        Target(std::string_view name, TargetType type);

        // 四组条目唯一的写入点：与 entry_count / entry_at 对称，非 include 条目不得带 is_system
        void append_entry(TargetEntryKind kind, ScopedEntry entry);

        std::string m_name = {};                                 // 目标名，CMake 唯一键
        TargetType  m_type = TargetType::None;                   // 目标类型

        std::vector<std::string> m_source_glob_dirs = {};        // 源通配目录，按登记顺序保序；空表示未登记

        Argument    m_compile_standard     = Argument::bare(kSupportedCompileStandard);
        bool        m_has_compile_standard = false;              // 是否调用过 set_compile_standard

        Argument    m_output_directory     = Argument::quoted("");   // 未设置时的占位取值
        bool        m_has_output_directory = false;                  // 是否调用过 set_output_directory

        FormOption  m_form_option     = FormOption{Argument::quoted(""), Argument::quoted(""), Argument::quoted("")};
        bool        m_has_form_option = false;                   // 是否调用过 set_form_option

        ImportSpec  m_import     = {};                           // 未设置时的空规格
        bool        m_has_import = false;                        // 是否调用过 set_import

        // 目标设置项（按配置输出目录、目标守卫、互斥分支、BUILD_RPATH、PIC）只登记取值，
        // 合法性一律由 validate(const Project&) 判定，模型层不自行拒绝
        ConfigOutputDirectories m_config_output_directories = {Argument::quoted(""), Argument::quoted("")};
        bool                    m_has_config_output_directories = false;   // 是否调用过 set_config_output_directories

        TargetGuard             m_guard     = {Argument::bare(""), Argument::quoted("")};
        bool                    m_has_guard = false;                       // 是否调用过 set_guard

        std::vector<BranchBlock> m_branches = {};                          // 互斥分支块，按插入顺序写出

        std::vector<std::string> m_build_rpath     = {};                   // 构建期运行搜索路径，按登记顺序保序
        bool                     m_has_build_rpath = false;                // 是否调用过 set_build_rpath

        bool m_position_independent_code     = false;                      // POSITION_INDEPENDENT_CODE 取值
        bool m_has_position_independent_code = false;                      // 是否调用过 set_position_independent_code

        std::array<std::vector<ScopedEntry>, to_target_entry_kind_uint(TargetEntryKind::Count)> m_entries = {};
    };
} // namespace ID::CMake
