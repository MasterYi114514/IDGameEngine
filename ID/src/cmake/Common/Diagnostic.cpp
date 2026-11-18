#include "cmake/Common/Diagnostic.hpp"

#include <cstdio>
#include <cstdlib>

#include "cmake/Common/Assert.hpp"

namespace ID::CMake
{
    std::string_view to_string(CMakeError error) noexcept
    {
        switch (error)
        {
            case CMakeError::EmptyName:                  return "EmptyName";
            case CMakeError::InvalidName:                return "InvalidName";
            case CMakeError::InvalidBareArgument:        return "InvalidBareArgument";
            case CMakeError::InvalidTargetType:          return "InvalidTargetType";
            case CMakeError::UnsupportedCompileStandard: return "UnsupportedCompileStandard";
            case CMakeError::InvalidSourceGlobDir:       return "InvalidSourceGlobDir";
            case CMakeError::DuplicateTarget:            return "DuplicateTarget";
            case CMakeError::DuplicateSubdirectory:      return "DuplicateSubdirectory";
            case CMakeError::DuplicateDerivedVariable:   return "DuplicateDerivedVariable";
            case CMakeError::MissingSourceGlobDir:       return "MissingSourceGlobDir";
            case CMakeError::InterfaceTargetScope:       return "InterfaceTargetScope";
            case CMakeError::InterfaceTargetSetting:     return "InterfaceTargetSetting";
            case CMakeError::InvalidFormOptionTarget:    return "InvalidFormOptionTarget";
            case CMakeError::MissingImportLocation:      return "MissingImportLocation";
            case CMakeError::ImportTargetSetting:        return "ImportTargetSetting";
            case CMakeError::ImportTargetScope:          return "ImportTargetScope";
            case CMakeError::VerbatimArgument:           return "VerbatimArgument";
            case CMakeError::LinkTargetNotFound:         return "LinkTargetNotFound";
            case CMakeError::PathEscapesProjectRoot:     return "PathEscapesProjectRoot";
            case CMakeError::TargetModified:             return "TargetModified";
            case CMakeError::MissingFile:                return "MissingFile";
            case CMakeError::MissingMarker:              return "MissingMarker";
            case CMakeError::AmbiguousMarker:            return "AmbiguousMarker";
            case CMakeError::IoFailed:                   return "IoFailed";
            case CMakeError::InvalidConfigOutput:        return "InvalidConfigOutput";
            case CMakeError::InvalidMacroBranch:         return "InvalidMacroBranch";
            case CMakeError::InvalidGuard:               return "InvalidGuard";
            case CMakeError::DuplicateFragment:          return "DuplicateFragment";
            case CMakeError::InvalidBuildRpath:          return "InvalidBuildRpath";
        }

        // 29 个类别全部在上表列出；只有用 static_cast 造出域外值时才到达这里，
        // 不把未知取值静默映射成某个具体类别，以免诊断指向错误对象
        return "Unknown";
    }

    // 断言失败路径：定长字符串逐个写出，不在终止路径上分配内存或做格式化拼接
    [[noreturn]] void assertion_failed(const char* expression, const char* file, std::size_t line) noexcept
    {
        std::fputs("assertion failed: ", stderr);
        std::fputs(expression, stderr);
        std::fputs(" at ", stderr);
        std::fputs(file, stderr);
        std::fputc(':', stderr);
        std::fprintf(stderr, "%zu\n", line);
        std::fflush(stderr);
        std::abort();
    }
} // namespace ID::CMake
