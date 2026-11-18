#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

/**
 *  与平台、locale 无关的文本工具：前缀/后缀判断、ASCII 大小写折叠、十进制与十六进制文本、
 *  展示用转义、控制字符判定与 CMake 字符串转义。
 *
 *  不变量：
 *  1. 所有大小写折叠只处理 ASCII（a–z / A–Z），不依赖 locale，也不做 Unicode 规范化；
 *  2. 数值文本按固定规则生成，不读 locale，不受千分位与小数点符号影响；
 *  3. 排序与相等比较一律按字节，不提供正则与 locale 相关比较（不引入 <regex>）。
 *
 *  离线工具、非运行时热路径：本模块不在逐帧或逐元素路径上，std::string / std::vector 的分配成本可接受，
 *  因此不做逐点分配失败检查——这是对 cpp-style 第 9 节「分配失败不得忽略」的显式例外（范围仅限容器与
 *  字符串分配），与 cmake/Common 的既有口径一致。
 */

namespace ID::Project
{
    /**
     *  @brief 判断文本是否以给定前缀开头
     *  @param text 待检查文本
     *  @param prefix 前缀；空前缀恒为真
     */
    bool starts_with(std::string_view text, std::string_view prefix) noexcept;

    /**
     *  @brief 判断文本是否以给定后缀结尾
     *  @param text 待检查文本
     *  @param suffix 后缀；空后缀恒为真
     */
    bool ends_with(std::string_view text, std::string_view suffix) noexcept;

    /**
     *  @brief 去掉两端的 ASCII 空白（空格、\t、\r、\n、\v、\f）
     *  @param text 待处理文本
     *  @return 去掉两端空白后的视图，指向 text 的存储
     */
    std::string_view trim_ascii_space(std::string_view text) noexcept;

    /**
     *  @brief ASCII 小写转大写，只处理 a–z，不依赖 locale
     *  @param text 输入文本
     *  @return 折叠后的新字符串；非 ASCII 字节原样保留
     */
    std::string ascii_upper(std::string_view text);

    /**
     *  @brief ASCII 大写转小写，只处理 A–Z，不依赖 locale
     *  @param text 输入文本
     *  @return 折叠后的新字符串；非 ASCII 字节原样保留
     */
    std::string ascii_lower(std::string_view text);

    /**
     *  @brief 只折叠 ASCII 大小写后的相等比较
     *  @param left 左操作数
     *  @param right 右操作数
     *  @return 长度相同且逐字节在 ASCII 折叠后相等时返回 true
     *  @note Windows 路径比较与保留名比较共用本函数；其它平台按精确字节比较的地方不调用它
     */
    bool ascii_iequals(std::string_view left, std::string_view right) noexcept;

    /**
     *  @brief 无符号十进制文本
     *  @param value 取值
     *  @return "0" 或十进制数字串，不含正号与前导零
     */
    std::string to_decimal(std::uint64_t value);

    /**
     *  @brief 有符号十进制文本
     *  @param value 取值
     *  @return 十进制数字串，负数带前导 '-'，不含前导零（零写 "0"）
     */
    std::string to_signed_decimal(std::int64_t value);

    /**
     *  @brief 字节串转小写十六进制
     *  @param bytes 待转换字节
     *  @return 长度等于字节数两倍的字符串，字符集为 0–9 与 a–f
     *  @note 记录里的摘要一律写这种形式（小写十六进制）
     */
    std::string to_lower_hex(std::span<const std::uint8_t> bytes);

    /**
     *  @brief 生成终端展示用的带引号文本
     *  @param text 输入文本
     *  @return 用双引号包裹并按 C 转义规则处理 \、"、\t、\r、\n 与其它 C0 控制字符（\xNN）的文本
     *  @note 只影响可读性，不参与命令执行，也不作为可复制的命令行片段
     */
    std::string quote_for_display(std::string_view text);

    /**
     *  @brief 把参数数组拼成可复制的展示文本
     *  @param arguments 参数数组，每个元素按 quote_for_display 处理，元素之间用一个空格分隔
     *  @return 展示文本；空数组返回空串
     *  @note 仅用于日志与诊断；真正执行仍走 argv 数组，不经过本函数的输出
     */
    std::string join_for_display(std::span<const std::string> arguments);

    /**
     *  @brief 判断文本是否含 CR/LF/TAB 之外的 C0 控制字符或 DEL
     *  @param text 待检查文本
     *  @return 含取值为 0x00–0x08、0x0B、0x0C、0x0E–0x1F 或 0x7F 的字节时返回 true
     *  @note 换行与制表符在 JSON 与 CMake 文本里可用，因此不算控制字符
     */
    bool contains_control_character(std::string_view text) noexcept;

    /**
     *  @brief 判断文本是否是合法的 POSIX 分隔相对路径形态
     *  @param text 待检查文本
     *  @return 不以 '/' 开头、不含反斜杠、不含盘符时的返回 true
     *  @note 本函数只做形态判断，不做规范化与越界判定；项目内路径的正式入口是
     *        PathText.hpp 的 normalize_relative
     */
    bool is_valid_posix_relative(std::string_view text) noexcept;

    /**
     *  @brief 转义 CMake 字符串字面量里的特殊字符
     *  @param text 原始文本
     *  @return 把 \ 与 " 各自补一个前置反斜杠后的文本
     *  @note 选项 description 进 option() 之前使用；变量展开符 ${} 不在此转义（由调用点决定是否允许）
     */
    std::string escape_cmake_string(std::string_view text);
} // namespace ID::Project
