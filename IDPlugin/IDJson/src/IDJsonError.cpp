#include "IDJsonBuilder.hpp"
#include "IDJsonParser.hpp"
#include "IDJsonWriter.hpp"

/**
 *  错误码到稳定文本的映射。
 *
 *  实现说明：
 *  - 用 switch 而不是查表：错误码只在失败冷路径上取文本，switch 让编译器在新增枚举值时给出
 *    "未处理取值"的诊断（-Wswitch），避免新增错误码后静默落到 default。
 *  - 返回编译期字符串字面量：不需要分配，可跨线程直接使用，生命周期是进程级。
 *  - 未知取值（强制转换出的越界值）返回 "unknown_error"，不返回 nullptr：调用方可以无条件
 *    把返回值当 C 字符串用。
 */

namespace ID::Json
{
    const char* to_string(JsonError error) noexcept
    {
        switch (error)
        {
            case JsonError::none:                   return "none";
            case JsonError::out_of_memory:          return "out_of_memory";
            case JsonError::unexpected_token:       return "unexpected_token";
            case JsonError::invalid_number:         return "invalid_number";
            case JsonError::invalid_string_escape:  return "invalid_string_escape";
            case JsonError::invalid_utf16_surrogate: return "invalid_utf16_surrogate";
            case JsonError::control_char_in_string: return "control_char_in_string";
            case JsonError::depth_exceeded:         return "depth_exceeded";
            case JsonError::trailing_content:       return "trailing_content";
            case JsonError::input_too_large:        return "input_too_large";
            case JsonError::not_initialized:        return "not_initialized";
        }
        return "unknown_error";
    }

    const char* to_string(JsonWriteError error) noexcept
    {
        switch (error)
        {
            case JsonWriteError::none:              return "none";
            case JsonWriteError::non_finite_number: return "non_finite_number";
            case JsonWriteError::too_deep:          return "too_deep";
        }
        return "unknown_error";
    }

    const char* to_string(JsonBuildError error) noexcept
    {
        switch (error)
        {
            case JsonBuildError::none:                  return "none";
            case JsonBuildError::not_initialized:       return "not_initialized";
            case JsonBuildError::out_of_memory:         return "out_of_memory";
            case JsonBuildError::depth_exceeded:        return "depth_exceeded";
            case JsonBuildError::root_already_set:      return "root_already_set";
            case JsonBuildError::no_open_object:        return "no_open_object";
            case JsonBuildError::no_open_container:     return "no_open_container";
            case JsonBuildError::no_pending_key:        return "no_pending_key";
            case JsonBuildError::key_already_pending:   return "key_already_pending";
            case JsonBuildError::key_without_value:     return "key_without_value";
            case JsonBuildError::too_large:             return "too_large";
        }
        return "unknown_error";
    }
} // namespace ID::Json
