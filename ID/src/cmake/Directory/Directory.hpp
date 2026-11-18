#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "cmake/Target/Target.hpp"
#include "cmake/Value/Argument.hpp"

/**
 *  目录层：一个 CMakeLists.txt 文件的模型——options、配置变量与配置块命令、目标、子目录与尾部命令。
 *
 *  职责边界：只存数据并提供只读面，不写文件、不递归编排、不判定链接目标名、不决定落盘模式。
 *  整文件或目录体文本由 emit_directory 产出，待落盘清单与遍历顺序由 Project 给出，
 *  结构问题由 validate(const Project&) 判定。目录层不认识插件，也不做任何插件名特判。
 *
 *  所有权：目标与子目录由本目录以 std::unique_ptr 持有，元素是堆上的独立对象，容器扩容不移动被指对象，
 *  因此 create_target / create_subdirectory 返回的引用在后续创建操作后仍然有效；这两个入口是唯一的
 *  构造入口（内部 make_unique 后入容器），外部不得构造游离对象。create_referenced_subdirectory 只登记
 *  名字、不构造对象、不进入落盘遍历。
 *
 *  离线工具、非运行时热路径：本模块不在逐帧或逐元素路径上，std::string / std::vector 的分配成本可接受，
 *  模型层因此不做逐点分配失败检查——这是对 cpp-style 第 9 节「分配失败不得忽略」的显式例外
 *  （依据：CMake 封装计划书第 1.2 节第 10 项，范围仅限容器与字符串分配）。
 */

namespace ID::CMake
{
    /**
     *  option(...) 的一条取值：名字、默认值开关与说明串。数据载体不带判定，
     *  名字形态由 validate 的参数检查报 InvalidBareArgument。
     */
    struct Option
    {
        Argument name;                    // 选项名，形如 IDJSON_ENABLE_LOG
        bool     is_default_on = false;   // 为真写 ON，否则写 OFF
        Argument description;             // 说明串
    };

    /**
     *  set(...) 的一条取值。is_cache 为真时追加 `CACHE STRING "<doc_string>"`。
     */
    struct Variable
    {
        Argument              name;                    // 变量名，形如 IDPLUGIN_PATH
        std::vector<Argument> values;                  // 取值序列，按插入顺序写出
        bool                  is_cache = false;        // 是否为 CACHE 变量
        Argument              doc_string;              // CACHE 变量的说明串
    };

    /**
     *  配置段（第 4 项）的单值变量定义：先写空值拒绝守卫，再写一条 set()。
     *  守卫形态固定为拒绝（message(FATAL_ERROR)），不给默认值、不做 fallback。
     *  数据载体不带判定：名字、取值与条件的合法性由 validate 判定。
     */
    struct ConfigurationVariable
    {
        Argument name;               // 变量名，形如 ID_OUTPUT_ROOT
        Argument value;              // 变量取值，由调用方给出字面量表达式
        Argument guard_condition;    // 守卫条件表达式，由调用方给出，模型不解析
        Argument guard_message;      // 守卫命中时的说明串
    };

    /**
     *  第 13 项的一条子目录条目。两种入口共用一个有序序列：
     *  is_referenced 为真表示只写 add_subdirectory、不建目录对象、不落盘。
     *  携带 source 表达式的条目按表达式写出，name 只作条目身份（去重与诊断定位）。
     */
    struct SubdirectoryEntry
    {
        std::string                name;                          // 目录名；无 source 表达式时同时是 add_subdirectory 的参数
        bool                       is_referenced = false;         // true：只登记名字，没有目录对象
        std::optional<Argument>    source;                        // 源目录表达式；空表示用 name
        std::optional<Argument>    binary;                        // 二进制目录表达式；空表示不写
        bool                       is_excluded_from_all = false;   // 为真时追加 EXCLUDE_FROM_ALL
        std::optional<TargetGuard> guard;                          // 接入前的 TARGET 拒绝守卫
    };

    /**
     *  单个 CMakeLists.txt 文件。实例只能由 Project 的构造函数（根目录）与本类的
     *  create_subdirectory（子目录）创建。
     */
    class Directory
    {
    public:
        Directory(const Directory&)            = delete;
        Directory& operator=(const Directory&) = delete;

        /**
         *  @brief 目录名；根目录名即工程名，用于 project(<名>)
         */
        std::string_view get_name() const noexcept;

        /**
         *  @brief 相对项目根的 POSIX 分隔路径：根为 "."，其余为 "IDPlugin" / "IDPlugin/IDJson"
         */
        std::string_view get_relative_path() const noexcept;

        /**
         *  @brief 本目录的目标数量
         */
        std::size_t target_count() const noexcept;

        /**
         *  @brief 取本目录的一个目标
         *  @param index 下标，必须小于 target_count
         *  @return 堆对象的引用，在后续创建操作后仍然有效
         *  @note 下标越界属调用方违约，用 ID_ASSERT 终止，不进诊断通道
         */
        const Target& target_at(std::size_t index) const noexcept;

        /**
         *  @brief 子目录条目数量（两种入口合计，就是第 13 项 add_subdirectory 的输出顺序与条数）
         */
        std::size_t subdirectory_entry_count() const noexcept;

        /**
         *  @brief 取一条子目录条目
         */
        const SubdirectoryEntry& subdirectory_entry_at(std::size_t index) const noexcept;

        /**
         *  @brief 取与子目录条目同下标的目录对象
         *  @param entry_index 子目录条目下标
         *  @return 目录对象指针；is_referenced 条目（只登记名字）返回空指针
         *  @note 返回的指针指向堆对象，在后续创建操作后仍然有效；空指针不得解引用
         */
        const Directory* subdirectory_at(std::size_t entry_index) const noexcept;

        /**
         *  @brief option 条目数量
         */
        std::size_t option_count() const noexcept;

        /**
         *  @brief 取一条 option 条目
         */
        const Option& option_at(std::size_t index) const noexcept;

        /**
         *  @brief set 变量条目数量
         */
        std::size_t variable_count() const noexcept;

        /**
         *  @brief 取一条 set 变量条目
         */
        const Variable& variable_at(std::size_t index) const noexcept;

        /**
         *  @brief 配置段"守卫 + 定义"条目的数量（第 4 项内，位于普通 set 之后、配置块命令之前）
         */
        std::size_t configuration_variable_count() const noexcept;

        /**
         *  @brief 取一条配置段"守卫 + 定义"条目
         *  @param index 下标，必须小于 configuration_variable_count
         *  @note 下标越界属调用方违约，用 ID_ASSERT 终止，不进诊断通道
         */
        const ConfigurationVariable& configuration_variable_at(std::size_t index) const noexcept;

        /**
         *  @brief 目录级全局编译选项数量
         */
        std::size_t global_compile_option_count() const noexcept;

        /**
         *  @brief 取一条目录级全局编译选项
         */
        const Argument& global_compile_option_at(std::size_t index) const noexcept;

        /**
         *  @brief 取与全局编译选项同下标的条件
         *  @return 条件指针；无条件的条目返回空指针（与 Target 条目的空 optional 同义）
         */
        const Argument* global_compile_option_condition_at(std::size_t index) const noexcept;

        /**
         *  @brief 配置块命令数量
         */
        std::size_t configuration_command_count() const noexcept;

        /**
         *  @brief 取一条配置块命令
         */
        const Argument& configuration_command_at(std::size_t index) const noexcept;

        /**
         *  @brief 第 13 项尾部命令数量
         */
        std::size_t trailing_command_count() const noexcept;

        /**
         *  @brief 取一条尾部命令
         */
        const Argument& trailing_command_at(std::size_t index) const noexcept;

        /**
         *  @brief project() 的语言参数数量；为 0 时省略 LANGUAGES 段
         */
        std::size_t language_count() const noexcept;

        /**
         *  @brief 取一个语言参数
         */
        const Argument& language_at(std::size_t index) const noexcept;

        /**
         *  @brief 追加一个 project() 语言参数
         */
        void add_language(Argument language);

        /**
         *  @brief 追加一条 option 条目；它紧跟在 project() 之后、其余所有内容之前输出
         */
        void add_option(Argument name, bool is_default_on, Argument description);

        /**
         *  @brief 追加一条 set 变量条目；落在第 4 项配置块内
         */
        void add_variable(Argument name, std::vector<Argument> values, bool is_cache, Argument doc_string);

        /**
         *  @brief 登记一条配置段"守卫 + 定义"：先写空值拒绝守卫，再写一条 set()
         *  @param name 变量名
         *  @param value 变量取值，由调用方给出字面量表达式
         *  @param guard_condition 守卫条件表达式，由调用方给出；模型不解析、不替调用方决定默认值
         *  @param guard_message 守卫命中时的说明串
         *  @note 本流程只调用一次：重复调用属调用方违约，用 ID_ASSERT 终止，不新增诊断类别；
         *        变量取值不含 $<CONFIG>、不由 CMAKE_BUILD_TYPE 派生属调用方契约（无对应诊断类别）
         */
        void add_configuration_variable(Argument name, Argument value, Argument guard_condition, Argument guard_message);

        /**
         *  @brief 追加一条目录级全局编译选项；落在第 4 项配置块的 add_compile_options 段
         *  @param option 编译选项取值
         *  @param condition 可选条件；空 optional 表示无条件
         */
        void add_global_compile_option(Argument option, std::optional<Argument> condition = std::nullopt);

        /**
         *  @brief 追加一条配置块命令
         *  @param command_line 原始词法片段；允许含 '\n'，emitter 逐行写出且不重排、不剥除内部缩进
         *  @note 受控逃生口：只落在第 4 项配置块末尾，不做顺序推断
         */
        void add_configuration_command(Argument command_line);

        /**
         *  @brief 创建一个目标并纳入本目录
         *  @param name 目标名；只登记，不拒绝空串、非法字符与哨兵类型，由 validate 判定
         *  @param type 目标类型
         *  @return 目标的引用，在后续创建操作后仍然有效
         */
        Target& create_target(std::string_view name, TargetType type);

        /**
         *  @brief 创建一个子目录：建目录对象并入落盘遍历，同时写一条 add_subdirectory
         *  @param name 子目录名；与 create_referenced_subdirectory 共用同一套名字校验与重名判定
         *  @return 子目录对象的引用，在后续创建操作后仍然有效
         */
        Directory& create_subdirectory(std::string_view name);

        /**
         *  @brief 只登记一个子目录名字：只在第 13 项写 add_subdirectory，不建目录对象、不进入落盘遍历
         *  @note 该名字下不能继续建模；需要给子目录写文件时必须用 create_subdirectory
         */
        void create_referenced_subdirectory(std::string_view name);

        /**
         *  @brief 登记一条带源/二进制目录表达式与可选守卫的子目录接入
         *        （`add_subdirectory(<source> [<binary>] [EXCLUDE_FROM_ALL])`）
         *  @param source 源目录表达式，按参数渲染后写出；它同时是条目身份（去重与诊断定位）
         *  @param binary 二进制目录表达式；传空片段表示不写该参数
         *  @param is_excluded_from_all 为真时追加 EXCLUDE_FROM_ALL
         *  @param guard 接入前的 TARGET 拒绝守卫；空 optional 表示不写守卫
         *  @note 只登记名字与表达式，不建目录对象、不进入落盘遍历；守卫紧贴在自己那条 add_subdirectory 之前
         */
        void create_referenced_subdirectory(Argument source,
                                            Argument binary,
                                            bool is_excluded_from_all,
                                            std::optional<TargetGuard> guard);

        /**
         *  @brief 追加一条尾部命令
         *  @param command_line 原始词法片段；允许含 '\n'，emitter 逐行写出且不重排、不剥除内部缩进
         *  @note 受控逃生口：只落在第 13 项，排在全部 add_subdirectory 之后
         */
        void add_trailing_command(Argument command_line);

    private:
        friend class Project;

        Directory(std::string_view name, std::string_view relative_path);

        std::string m_name          = {};   // 目录名，根目录名即工程名
        std::string m_relative_path = {};   // 相对项目根的 POSIX 分隔路径

        std::vector<std::unique_ptr<Target>>    m_targets             = {};   // 本目录的目标
        std::vector<std::unique_ptr<Directory>> m_subdirectories      = {};   // is_referenced 为假的子目录对象
        std::vector<SubdirectoryEntry>          m_subdirectory_entries = {}; // 两种入口共用的单一有序序列

        std::vector<Option>                 m_options        = {};
        std::vector<Variable>               m_variables      = {};
        std::vector<ConfigurationVariable>  m_configuration_variables = {};   // 每目录至多一条（add_configuration_variable 断言）
        std::vector<Argument>               m_languages      = {};

        // 全局编译选项与其条件按同一下标成对追加，两个容器由 add_global_compile_option 唯一维护
        std::vector<Argument>               m_global_compile_options            = {};
        std::vector<std::optional<Argument>> m_global_compile_option_conditions = {};

        std::vector<Argument>               m_configuration_commands = {};
        std::vector<Argument>               m_trailing_commands      = {};
    };
} // namespace ID::CMake
