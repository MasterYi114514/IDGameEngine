#include "id_project_test_support.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "benchmark_id.hpp"
#include "benchmark_support.hpp"
#include "project/Base/Inject.hpp"
#include "project/Cli/Runner.hpp"

#if defined(_WIN32)
    #if !defined(WIN32_LEAN_AND_MEAN)
        #define WIN32_LEAN_AND_MEAN
    #endif
    #if !defined(NOMINMAX)
        #define NOMINMAX
    #endif
    #include <windows.h>
#endif

/**
 *  `id.project` 子域测试支撑实现。
 *
 *  设计取舍：
 *  - 进程内驱动复用 `ID::Project::run_command_line`（`Runner.hpp` 明确写明供 `Benchmark` 使用），
 *    断言的是同一份实现；`Abort` 注入会 `_Exit` 调用方，必须走子进程。
 *  - 摘要是测试侧独立实现的 SHA-256：被测实现的 `Digest` 不参与本文件的任何摘要计算，
 *    否则"记录摘要与磁盘一致"会变成拿被测实现证明自己。
 *  - 快照只覆盖项目根与假引擎根 `ID/`；`engine/IDPlugin` 是被测输入，不并入"未写盘"判定。
 */

#if !defined(BENCHMARK_TEMP_DIR)
    #define BENCHMARK_TEMP_DIR ""
#endif

#if !defined(BENCHMARK_ID_EXE)
    #define BENCHMARK_ID_EXE ""
#endif

#if !defined(BENCHMARK_WINDOW_SRC_DIR)
    #define BENCHMARK_WINDOW_SRC_DIR ""
#endif

namespace
{
    using ID::Bench::IdProject::CommandResult;
    using ID::Bench::IdProject::JsonReader;
    using ID::Bench::IdProject::SpawnOptions;
    using ID::Bench::IdProject::SpawnResult;

    std::string g_session_root = {};
    bool        g_is_digest_ok = false;

    // ---------------- 基础文本/路径 ----------------

    std::string JoinPath(const std::string& base, std::string_view relative) noexcept
    {
        if (relative.empty())
        {
            return base;
        }

        std::string path = base;
        if (!path.empty() && path.back() != '/')
        {
            path.push_back('/');
        }
        path.append(relative);
        return path;
    }

    std::string ToNativeSeparators(std::string text) noexcept
    {
        // std::filesystem 在 Windows 上能同时吃 '/'，这里保持 POSIX 形式避免二次转换
        return text;
    }

    bool StartsWith(std::string_view text, std::string_view prefix) noexcept
    {
        return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
    }

    // ---------------- SHA-256（测试侧独立实现） ----------------

    inline constexpr std::uint32_t kSha256RoundConstants[64] = {
        0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
        0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
        0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
        0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
        0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
        0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
        0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
        0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
    };

    inline constexpr std::uint32_t kSha256InitialState[8] = {
        0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u,
    };

    std::uint32_t RotateRight(std::uint32_t value, unsigned bits) noexcept
    {
        return (value >> bits) | (value << (32u - bits));
    }

    void Sha256Compress(std::uint32_t* state, const unsigned char* block) noexcept
    {
        std::uint32_t schedule[64] = {};

        for (std::size_t index = 0; index < 16; ++index)
        {
            schedule[index] = (static_cast<std::uint32_t>(block[index * 4]) << 24)
                            | (static_cast<std::uint32_t>(block[index * 4 + 1]) << 16)
                            | (static_cast<std::uint32_t>(block[index * 4 + 2]) << 8)
                            | static_cast<std::uint32_t>(block[index * 4 + 3]);
        }

        for (std::size_t index = 16; index < 64; ++index)
        {
            const std::uint32_t s0 = RotateRight(schedule[index - 15], 7) ^ RotateRight(schedule[index - 15], 18)
                                   ^ (schedule[index - 15] >> 3);
            const std::uint32_t s1 = RotateRight(schedule[index - 2], 17) ^ RotateRight(schedule[index - 2], 19)
                                   ^ (schedule[index - 2] >> 10);
            schedule[index] = schedule[index - 16] + s0 + schedule[index - 7] + s1;
        }

        std::uint32_t a = state[0];
        std::uint32_t b = state[1];
        std::uint32_t c = state[2];
        std::uint32_t d = state[3];
        std::uint32_t e = state[4];
        std::uint32_t f = state[5];
        std::uint32_t g = state[6];
        std::uint32_t h = state[7];

        for (std::size_t index = 0; index < 64; ++index)
        {
            const std::uint32_t s1    = RotateRight(e, 6) ^ RotateRight(e, 11) ^ RotateRight(e, 25);
            const std::uint32_t ch    = (e & f) ^ ((~e) & g);
            const std::uint32_t temp1 = h + s1 + ch + kSha256RoundConstants[index] + schedule[index];
            const std::uint32_t s0    = RotateRight(a, 2) ^ RotateRight(a, 13) ^ RotateRight(a, 22);
            const std::uint32_t maj   = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t temp2 = s0 + maj;

            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }

        state[0] += a;
        state[1] += b;
        state[2] += c;
        state[3] += d;
        state[4] += e;
        state[5] += f;
        state[6] += g;
        state[7] += h;
    }

    std::string Sha256HexImpl(std::string_view bytes) noexcept
    {
        std::uint32_t state[8] = {};
        for (std::size_t index = 0; index < 8; ++index)
        {
            state[index] = kSha256InitialState[index];
        }

        std::size_t offset = 0;
        while (bytes.size() - offset >= 64)
        {
            Sha256Compress(state, reinterpret_cast<const unsigned char*>(bytes.data() + offset));
            offset += 64;
        }

        // 尾部：剩余字节 + 0x80 + 补零 + 64 位大端比特长度（单块或双块）
        unsigned char tail[128] = {};
        const std::size_t rest  = bytes.size() - offset;
        for (std::size_t index = 0; index < rest; ++index)
        {
            tail[index] = static_cast<unsigned char>(bytes[offset + index]);
        }
        tail[rest] = 0x80u;

        const std::size_t tail_blocks_size = (rest + 1 + 8 <= 64) ? 64 : 128;
        const std::uint64_t bit_length     = static_cast<std::uint64_t>(bytes.size()) * 8u;
        for (std::size_t index = 0; index < 8; ++index)
        {
            tail[tail_blocks_size - 1 - index] = static_cast<unsigned char>((bit_length >> (index * 8)) & 0xffu);
        }

        Sha256Compress(state, tail);
        if (tail_blocks_size == 128)
        {
            Sha256Compress(state, tail + 64);
        }

        static constexpr char kHexDigits[] = "0123456789abcdef";
        std::string           hex;
        hex.reserve(64);
        for (const std::uint32_t word : state)
        {
            for (int shift = 28; shift >= 0; shift -= 4)
            {
                hex.push_back(kHexDigits[(word >> shift) & 0xfu]);
            }
        }
        return hex;
    }

    bool RunDigestSelfTest() noexcept
    {
        const std::string empty_digest = Sha256HexImpl(std::string_view());
        const std::string abc_digest   = Sha256HexImpl(std::string_view("abc"));

        return empty_digest == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
            && abc_digest == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
    }

    // ---------------- 平台辅助 ----------------

    std::string TrimTrailingSlash(std::string path) noexcept
    {
        while (!path.empty() && (path.back() == '/' || path.back() == '\\'))
        {
            path.pop_back();
        }
        return path;
    }

#if defined(_WIN32)
    std::wstring ToWide(std::string_view text) noexcept
    {
        if (text.empty())
        {
            return std::wstring();
        }

        const int length = ::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
        if (length <= 0)
        {
            return std::wstring();
        }

        std::wstring wide(static_cast<std::size_t>(length), L'\0');
        ::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), length);
        return wide;
    }

    /**
     *  先摘掉目录树里的重解析点（junction），再交给 `std::filesystem::remove_all`。
     *
     *  为什么需要：本机 MinGW + UCRT 的 `remove_all` 把 junction 当普通目录递归进去，先删掉链接目标里的
     *  内容，再留下一个删不掉的目录项；用例根因此跨运行残留，下一次运行的 `CaseRoot` 读到上一次的
     *  `plugins.json`（实测 j2 的 `new` 报 `selection_source_conflict`、c12 依赖残留状态）。`RemoveDirectoryW`
     *  只摘链接本身、不触碰链接目标。
     */
    void DetachReparsePoints(const std::filesystem::path& directory) noexcept
    {
        WIN32_FIND_DATAW data{};
        const std::wstring pattern = (directory / L"*").wstring();
        HANDLE             handle  = ::FindFirstFileW(pattern.c_str(), &data);
        if (handle == INVALID_HANDLE_VALUE)
        {
            return;
        }

        std::vector<std::wstring> links;
        std::vector<std::wstring> children;
        do
        {
            const std::wstring name(data.cFileName);
            if (name == L"." || name == L"..")
            {
                continue;
            }
            const std::filesystem::path child = directory / name;
            if ((data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
            {
                links.push_back(child.wstring());
                continue;
            }
            if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
            {
                children.push_back(child.wstring());
            }
        } while (::FindNextFileW(handle, &data) != FALSE);
        ::FindClose(handle);

        for (const std::wstring& link : links)
        {
            ::RemoveDirectoryW(link.c_str());
        }
        for (const std::wstring& child : children)
        {
            DetachReparsePoints(std::filesystem::path(child));
        }
    }

    bool IsOverriddenEntry(const std::wstring& entry, std::wstring_view key) noexcept
    {
        return entry.size() > key.size() && entry.compare(0, key.size(), key) == 0;
    }

    std::vector<wchar_t> BuildEnvironment(const SpawnOptions& options, const std::string& engine_root) noexcept
    {
        std::vector<std::wstring> entries;

        LPWCH block = ::GetEnvironmentStringsW();
        if (block != nullptr)
        {
            for (const wchar_t* cursor = block; *cursor != L'\0'; cursor += std::wcslen(cursor) + 1)
            {
                std::wstring entry(cursor);
                if (IsOverriddenEntry(entry, L"ID_ENGINE_ROOT=")
                    || IsOverriddenEntry(entry, L"ID_TEST_INJECT=")
                    || (options.is_path_overridden && IsOverriddenEntry(entry, L"PATH="))
                    || (options.is_generator_overridden && IsOverriddenEntry(entry, L"CMAKE_GENERATOR=")))
                {
                    continue;
                }
                entries.push_back(std::move(entry));
            }
            ::FreeEnvironmentStringsW(block);
        }

        entries.push_back(L"ID_ENGINE_ROOT=" + ToWide(engine_root));
        if (!options.inject.empty())
        {
            entries.push_back(L"ID_TEST_INJECT=" + ToWide(options.inject));
        }
        if (options.is_path_overridden)
        {
            entries.push_back(L"PATH=" + ToWide(options.path_value));
        }
        if (options.is_generator_overridden)
        {
            entries.push_back(L"CMAKE_GENERATOR=" + ToWide(options.generator_value));
        }

        std::vector<wchar_t> buffer;
        for (const std::wstring& entry : entries)
        {
            buffer.insert(buffer.end(), entry.begin(), entry.end());
            buffer.push_back(L'\0');
        }
        buffer.push_back(L'\0');
        return buffer;
    }
#endif

} // namespace

namespace ID::Bench::IdProject
{
    std::string SessionRoot() noexcept
    {
        return g_session_root;
    }

    bool IsDigestSelfTestOk() noexcept
    {
        return g_is_digest_ok;
    }

    void BeginIdProjectSession() noexcept
    {
        g_is_digest_ok = RunDigestSelfTest();

        const std::string temp_root(BENCHMARK_TEMP_DIR);
        if (temp_root.empty())
        {
            g_session_root.clear();
            return;
        }

        g_session_root = TrimTrailingSlash(temp_root) + "/id.project";

        std::error_code error;
        std::filesystem::remove_all(std::filesystem::path(g_session_root), error);
        std::filesystem::create_directories(std::filesystem::path(g_session_root), error);
    }

    void EndIdProjectSession() noexcept
    {
        if (g_session_root.empty())
        {
            return;
        }

        // 走 RemoveTree（先摘 junction）：残留会把上一轮的用例夹具带进下一轮
        RemoveTree(g_session_root);
    }

    // ---------------- CaseRoot ----------------

    CaseRoot::CaseRoot(std::string_view case_id) noexcept
    {
        m_dir         = JoinPath(g_session_root, case_id);
        m_engine      = JoinPath(m_dir, "engine");
        m_plugin      = JoinPath(m_engine, "IDPlugin");
        m_project     = JoinPath(m_dir, "project");

        // 走 RemoveTree（先摘 junction）：上一轮同名用例根里可能残留 junction，直接 remove_all
        // 会删掉链接目标内容却留下删不掉的目录项，夹具状态因此串到下一轮
        RemoveTree(m_dir);

        std::error_code error;
        std::filesystem::create_directories(std::filesystem::path(JoinPath(m_engine, "ID")), error);
        std::filesystem::create_directories(std::filesystem::path(m_plugin), error);
        std::filesystem::create_directories(std::filesystem::path(m_project), error);
    }

    CaseRoot::~CaseRoot() noexcept
    {
        if (m_dir.empty())
        {
            return;
        }

        // 走 RemoveTree（先摘 junction）：用例根里可能存在 C12/J2 造的目录 junction
        RemoveTree(m_dir);
    }

    const std::string& CaseRoot::dir() const noexcept
    {
        return m_dir;
    }

    const std::string& CaseRoot::engine_root() const noexcept
    {
        return m_engine;
    }

    const std::string& CaseRoot::plugin_root() const noexcept
    {
        return m_plugin;
    }

    const std::string& CaseRoot::project_root() const noexcept
    {
        return m_project;
    }

    std::string CaseRoot::CasePath(std::string_view relative) const
    {
        return JoinPath(m_dir, relative);
    }

    std::string CaseRoot::ProjectPath(std::string_view relative) const
    {
        return JoinPath(m_project, relative);
    }

    std::string CaseRoot::EnginePath(std::string_view relative) const
    {
        return JoinPath(m_engine, relative);
    }

    void CaseRoot::ArchiveEvidence(std::string_view label) const noexcept
    {
        if (m_dir.empty() || label.empty())
        {
            return;
        }

        // 证据落 <benchmark>/build/id.project/<label>/；benchmark 根由构建期注入的 src 目录反推
        const std::string src_dir(BENCHMARK_WINDOW_SRC_DIR);
        if (src_dir.empty())
        {
            return;
        }

        std::filesystem::path benchmark_dir = std::filesystem::path(src_dir).parent_path();
        if (benchmark_dir.empty())
        {
            return;
        }

        const std::string target = (benchmark_dir / "build" / "id.project" / std::string(label)).string();

        std::error_code error;
        std::filesystem::remove_all(std::filesystem::path(target), error);
        std::filesystem::create_directories(std::filesystem::path(target).parent_path(), error);
        std::filesystem::copy(std::filesystem::path(m_dir), std::filesystem::path(target),
                              std::filesystem::copy_options::recursive, error);
    }

    // ---------------- 调用 ----------------

    namespace
    {
        // 进程内入口接收的是 `project` 之后的参数（`main.cpp` 用 subspan(2) 调用）；
        // 用例里为了可读性照抄完整命令行，因此这里剥掉开头的 "project"
        std::span<const std::string> StripProject(std::span<const std::string> arguments) noexcept
        {
            if (!arguments.empty() && arguments.front() == "project")
            {
                return arguments.subspan(1);
            }
            return arguments;
        }
    } // namespace

    CommandResult RunProject(const CaseRoot& root, std::span<const std::string> arguments) noexcept
    {
        arguments = StripProject(arguments);

        ID::Project::RunnerContext context;
        context.engine_root   = std::filesystem::path(root.engine_root());
        context.registry_path = std::filesystem::path(RegistryPath(root));
        context.plugin_root   = std::filesystem::path(root.plugin_root());

        std::ostringstream out_stream;
        std::ostringstream err_stream;
        context.io = ID::Project::CommandIo{&out_stream, &err_stream};

        // 每个用例自带注入状态：先清空，避免上一个用例的注入残留
        ID::Project::init_injection(std::string_view());
        context.is_injection_initialized = true;

        CommandResult result;
        result.exit_code = ID::Project::run_command_line(arguments, context);
        result.out_text  = out_stream.str();
        result.err_text  = err_stream.str();
        return result;
    }

    CommandResult RunProject(const CaseRoot& root, std::initializer_list<std::string_view> arguments) noexcept
    {
        std::vector<std::string> owned;
        owned.reserve(arguments.size());
        for (const std::string_view argument : arguments)
        {
            owned.emplace_back(argument);
        }
        return RunProject(root, std::span<const std::string>(owned.data(), owned.size()));
    }

    namespace
    {
        CommandResult RunInjected(const CaseRoot& root, std::span<const std::string> arguments,
                                  std::string_view inject) noexcept
        {
            arguments = StripProject(arguments);

            ID::Project::RunnerContext context;
            context.engine_root   = std::filesystem::path(root.engine_root());
            context.registry_path = std::filesystem::path(RegistryPath(root));
            context.plugin_root   = std::filesystem::path(root.plugin_root());

            std::ostringstream out_stream;
            std::ostringstream err_stream;
            context.io = ID::Project::CommandIo{&out_stream, &err_stream};

            ID::Project::init_injection(inject);
            context.is_injection_initialized = true;

            CommandResult result;
            result.exit_code = ID::Project::run_command_line(arguments, context);
            result.out_text  = out_stream.str();
            result.err_text  = err_stream.str();

            ID::Project::init_injection(std::string_view());
            return result;
        }
    } // namespace

    CommandResult RunProjectInjected(const CaseRoot& root, std::span<const std::string> arguments,
                                     std::string_view inject) noexcept
    {
        return RunInjected(root, arguments, inject);
    }

    CommandResult RunProjectInjected(const CaseRoot& root, std::initializer_list<std::string_view> arguments,
                                     std::string_view inject) noexcept
    {
        std::vector<std::string> owned;
        owned.reserve(arguments.size());
        for (const std::string_view argument : arguments)
        {
            owned.emplace_back(argument);
        }
        return RunInjected(root, std::span<const std::string>(owned.data(), owned.size()), inject);
    }

#if defined(_WIN32)
    SpawnResult SpawnProject(const CaseRoot& root, std::string_view tag, std::span<const std::string> arguments,
                             const SpawnOptions& options) noexcept
    {
        SpawnResult result;

        const std::string tool(BENCHMARK_ID_EXE);
        if (tool.empty())
        {
            return result;
        }

        const std::string spawn_dir = root.CasePath("spawn");
        std::error_code   error;
        std::filesystem::create_directories(std::filesystem::path(spawn_dir), error);

        const std::string stdout_path = JoinPath(spawn_dir, std::string(tag) + ".stdout.txt");
        const std::string stderr_path = JoinPath(spawn_dir, std::string(tag) + ".stderr.txt");
        const std::string stdin_path  = JoinPath(spawn_dir, "empty-stdin.txt");

        if (!WriteText(stdin_path, std::string_view()))
        {
            return result;
        }

        SECURITY_ATTRIBUTES security{};
        security.nLength        = sizeof(security);
        security.bInheritHandle = TRUE;

        const std::wstring wide_stdout = ToWide(stdout_path);
        const std::wstring wide_stderr = ToWide(stderr_path);
        const std::wstring wide_stdin  = ToWide(stdin_path);

        HANDLE stdout_handle = ::CreateFileW(wide_stdout.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                            &security, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (stdout_handle == INVALID_HANDLE_VALUE)
        {
            return result;
        }

        HANDLE stderr_handle = ::CreateFileW(wide_stderr.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                            &security, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (stderr_handle == INVALID_HANDLE_VALUE)
        {
            ::CloseHandle(stdout_handle);
            return result;
        }

        HANDLE stdin_handle = ::CreateFileW(wide_stdin.c_str(), GENERIC_READ, FILE_SHARE_READ, &security, OPEN_EXISTING,
                                            FILE_ATTRIBUTE_NORMAL, nullptr);
        if (stdin_handle == INVALID_HANDLE_VALUE)
        {
            stdin_handle = ::GetStdHandle(STD_INPUT_HANDLE);
        }

        STARTUPINFOW startup{};
        startup.cb         = sizeof(startup);
        startup.dwFlags    = STARTF_USESTDHANDLES;
        startup.hStdOutput = stdout_handle;
        startup.hStdError  = stderr_handle;
        startup.hStdInput  = stdin_handle;

        // 参数逐个加引号：路径含空格与中文时不二次分词
        std::wstring command_line = L"\"" + ToWide(tool) + L"\"";
        for (const std::string& argument : arguments)
        {
            command_line += L" \"";
            command_line += ToWide(argument);
            command_line += L"\"";
        }

        std::vector<wchar_t> environment = BuildEnvironment(options, root.engine_root());
        const std::wstring   working_directory = ToWide(root.dir());

        PROCESS_INFORMATION process{};
        const BOOL created = ::CreateProcessW(nullptr, command_line.data(), nullptr, nullptr, TRUE,
                                              CREATE_UNICODE_ENVIRONMENT, environment.data(), working_directory.c_str(),
                                              &startup, &process);
        ::CloseHandle(stdout_handle);
        ::CloseHandle(stderr_handle);
        if (stdin_handle != INVALID_HANDLE_VALUE && stdin_handle != ::GetStdHandle(STD_INPUT_HANDLE))
        {
            ::CloseHandle(stdin_handle);
        }

        if (created == FALSE)
        {
            return result;
        }
        result.is_spawned = true;

        if (::WaitForSingleObject(process.hProcess, options.timeout_ms) == WAIT_OBJECT_0)
        {
            DWORD exit_code = 0;
            if (::GetExitCodeProcess(process.hProcess, &exit_code) != FALSE)
            {
                result.exit_code = static_cast<int>(exit_code);
            }
        }
        else
        {
            result.is_timed_out = true;
            ::TerminateProcess(process.hProcess, 1);
            ::WaitForSingleObject(process.hProcess, 5000);
        }

        ::CloseHandle(process.hThread);
        ::CloseHandle(process.hProcess);

        ReadText(stdout_path, result.out_text);
        ReadText(stderr_path, result.err_text);
        return result;
    }
#else
    SpawnResult SpawnProject(const CaseRoot&, std::string_view, std::span<const std::string>,
                             const SpawnOptions&) noexcept
    {
        // 非 Windows 平台尚无子进程隔离（framework.md 第 9 节）：返回未启动，由调用方登记 Uncovered
        return SpawnResult{};
    }
#endif

    SpawnResult SpawnProject(const CaseRoot& root, std::string_view tag,
                             std::initializer_list<std::string_view> arguments,
                             const SpawnOptions& options) noexcept
    {
        std::vector<std::string> owned;
        owned.reserve(arguments.size());
        for (const std::string_view argument : arguments)
        {
            owned.emplace_back(argument);
        }
        return SpawnProject(root, tag, std::span<const std::string>(owned.data(), owned.size()), options);
    }

    // ---------------- 判定辅助 ----------------

    std::string Describe(const CommandResult& result) noexcept
    {
        constexpr std::size_t kMaxText = 600;

        const auto clip = [](const std::string& text) -> std::string
        {
            if (text.size() <= kMaxText)
            {
                return text;
            }
            return text.substr(0, kMaxText) + "...(truncated)";
        };

        return "exit=" + std::to_string(result.exit_code) + " | stdout=[" + clip(result.out_text) + "] | stderr=["
             + clip(result.err_text) + "]";
    }

    bool CheckExit(std::string_view name, const CommandResult& result, int expected,
                   std::string_view expectation) noexcept
    {
        const bool is_ok = result.exit_code == expected;
        ID::Bench::Check(is_ok, name,
                         std::string(expectation) + "（期望退出码 " + std::to_string(expected) + "）；" + Describe(result));
        return is_ok;
    }

    bool CheckExitOneOf(std::string_view name, const CommandResult& result, std::initializer_list<int> expected,
                        std::string_view expectation) noexcept
    {
        bool        is_ok         = false;
        std::string expected_text;
        for (const int value : expected)
        {
            is_ok = is_ok || (result.exit_code == value);
            if (!expected_text.empty())
            {
                expected_text += "/";
            }
            expected_text += std::to_string(value);
        }

        ID::Bench::Check(is_ok, name,
                         std::string(expectation) + "（期望退出码 " + expected_text + "）；" + Describe(result));
        return is_ok;
    }

    bool CheckSpawnExit(std::string_view name, const SpawnResult& result, int expected,
                        std::string_view expectation) noexcept
    {
        const bool is_ok = result.is_spawned && !result.is_timed_out && result.exit_code == expected;
        ID::Bench::Check(is_ok, name,
                         std::string(expectation) + "（期望已启动、未超时、退出码 " + std::to_string(expected)
                             + "，实际 spawned=" + (result.is_spawned ? "true" : "false")
                             + " timeout=" + (result.is_timed_out ? "true" : "false") + "）；" + Describe(result));
        return is_ok;
    }

    bool HasCategory(const CommandResult& result, std::string_view category) noexcept
    {
        return result.out_text.find(category) != std::string::npos || result.err_text.find(category) != std::string::npos;
    }

    bool Contains(const CommandResult& result, std::string_view needle) noexcept
    {
        return result.out_text.find(needle) != std::string::npos || result.err_text.find(needle) != std::string::npos;
    }

    bool AddExit(CaseVerdict& verdict, std::string_view label, const CommandResult& result, int expected) noexcept
    {
        if (result.exit_code == expected)
        {
            verdict.Add(true, label);
            return true;
        }

        verdict.Add(false, std::string(label) + "[exit=" + std::to_string(result.exit_code) + "]");

        const std::string& channel = result.err_text.empty() ? result.out_text : result.err_text;
        std::string        excerpt = channel;
        for (char& unit : excerpt)
        {
            if (unit == '\n' || unit == '\r')
            {
                unit = ' ';
            }
        }
        if (excerpt.size() > 240)
        {
            excerpt = excerpt.substr(0, 240) + "...";
        }
        verdict.Note("diag", excerpt);
        return false;
    }

    bool AddExitOneOf(CaseVerdict& verdict, std::string_view label, const CommandResult& result,
                      std::initializer_list<int> expected) noexcept
    {
        bool        is_ok = false;
        std::string expected_text;
        for (const int value : expected)
        {
            is_ok = is_ok || (result.exit_code == value);
            if (!expected_text.empty())
            {
                expected_text += "/";
            }
            expected_text += std::to_string(value);
        }
        if (is_ok)
        {
            verdict.Add(true, label);
            return true;
        }

        verdict.Add(false, std::string(label) + "[exit=" + std::to_string(result.exit_code) + "]");

        const std::string& channel = result.err_text.empty() ? result.out_text : result.err_text;
        std::string        excerpt = channel;
        for (char& unit : excerpt)
        {
            if (unit == '\n' || unit == '\r')
            {
                unit = ' ';
            }
        }
        if (excerpt.size() > 240)
        {
            excerpt = excerpt.substr(0, 240) + "...";
        }
        verdict.Note("expected " + expected_text + ", diag", excerpt);
        return false;
    }

    std::string CategoryList(const CommandResult& result) noexcept
    {
        // 类别标识是 ASCII snake_case：从"类别:"之后取连续的 [a-z0-9_]，不依赖全角括号的字节形态
        static constexpr std::string_view kMarker = "类别:";

        std::string        categories;
        const std::string* channels[2] = { &result.err_text, &result.out_text };
        for (const std::string* channel_ptr : channels)
        {
            const std::string& channel = *channel_ptr;
            std::size_t        begin   = 0;
            while ((begin = channel.find(kMarker, begin)) != std::string::npos)
            {
                begin += kMarker.size();
                while (begin < channel.size() && (channel[begin] == ' ' || channel[begin] == '\t'))
                {
                    ++begin;
                }

                std::size_t stop = begin;
                while (stop < channel.size())
                {
                    const char unit = channel[stop];
                    const bool is_word = (unit >= 'a' && unit <= 'z') || (unit >= '0' && unit <= '9') || unit == '_';
                    if (!is_word)
                    {
                        break;
                    }
                    ++stop;
                }

                if (stop > begin)
                {
                    if (!categories.empty())
                    {
                        categories += ",";
                    }
                    categories.append(channel, begin, stop - begin);
                }
                begin = stop > begin ? stop : begin + 1;
            }
        }
        return categories.empty() ? std::string("<none>") : categories;
    }

    bool CheckCategory(CaseVerdict& verdict, std::string_view label, const CommandResult& result,
                       std::string_view category) noexcept
    {
        const bool is_ok = HasCategory(result, category);
        verdict.Add(is_ok, label);
        if (!is_ok)
        {
            verdict.Note("diag", CategoryList(result) + "|exit=" + std::to_string(result.exit_code));
        }
        return is_ok;
    }

    void CaseVerdict::Add(bool is_ok, std::string_view label) noexcept
    {
        if (!m_detail.empty())
        {
            m_detail += "; ";
        }
        m_detail += std::string(label) + (is_ok ? "=ok" : "=FAIL");
        m_is_ok = m_is_ok && is_ok;
    }

    void CaseVerdict::Note(std::string_view label, std::string_view actual) noexcept
    {
        if (!m_detail.empty())
        {
            m_detail += "; ";
        }
        m_detail += std::string(label) + "=" + std::string(actual);
    }

    bool CaseVerdict::IsOk() const noexcept
    {
        return m_is_ok;
    }

    const std::string& CaseVerdict::Detail() const noexcept
    {
        return m_detail;
    }

    // ---------------- 文件与目录 ----------------

    bool WriteText(const std::string& path, std::string_view text) noexcept
    {
        std::error_code error;
        const std::filesystem::path target(ToNativeSeparators(path));
        if (target.has_parent_path())
        {
            std::filesystem::create_directories(target.parent_path(), error);
        }

        std::ofstream stream(target, std::ios::binary | std::ios::trunc);
        if (!stream)
        {
            return false;
        }

        stream.write(text.data(), static_cast<std::streamsize>(text.size()));
        stream.flush();
        return stream.good();
    }

    bool ReadText(const std::string& path, std::string& out) noexcept
    {
        out.clear();

        std::ifstream stream(std::filesystem::path(ToNativeSeparators(path)), std::ios::binary);
        if (!stream)
        {
            return false;
        }

        out.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
        return !stream.bad();
    }

    bool Exists(const std::string& path) noexcept
    {
        std::error_code error;
        return std::filesystem::exists(std::filesystem::path(ToNativeSeparators(path)), error);
    }

    bool IsRegularFile(const std::string& path) noexcept
    {
        std::error_code error;
        return std::filesystem::is_regular_file(std::filesystem::path(ToNativeSeparators(path)), error);
    }

    bool IsDirectory(const std::string& path) noexcept
    {
        std::error_code error;
        return std::filesystem::is_directory(std::filesystem::path(ToNativeSeparators(path)), error);
    }

    bool RemoveTree(const std::string& path) noexcept
    {
        const std::filesystem::path native(ToNativeSeparators(path));
#if defined(_WIN32)
        // 先摘掉 junction：否则 remove_all 会递归进链接目标，并留下删不掉的目录项（见 DetachReparsePoints）
        DetachReparsePoints(native);
#endif
        std::error_code error;
        std::filesystem::remove_all(native, error);
        return !error;
    }

    bool CreateDirectories(const std::string& path) noexcept
    {
        std::error_code error;
        std::filesystem::create_directories(std::filesystem::path(ToNativeSeparators(path)), error);
        return !error;
    }

    bool CopyTree(const std::string& from, const std::string& to) noexcept
    {
        std::error_code error;
        std::filesystem::copy(std::filesystem::path(ToNativeSeparators(from)),
                              std::filesystem::path(ToNativeSeparators(to)),
                              std::filesystem::copy_options::recursive
                                  | std::filesystem::copy_options::overwrite_existing,
                              error);
        return !error;
    }

    bool CreateJunction(const std::string& link_path, const std::string& target_path) noexcept
    {
#if defined(_WIN32)
        const std::wstring command_line = L"cmd.exe /c mklink /J \"" + ToWide(link_path) + L"\" \""
                                        + ToWide(target_path) + L"\"";

        STARTUPINFOW        startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        std::vector<wchar_t> buffer(command_line.begin(), command_line.end());
        buffer.push_back(L'\0');

        if (::CreateProcessW(nullptr, buffer.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
                             &startup, &process)
            == FALSE)
        {
            return false;
        }

        const DWORD wait = ::WaitForSingleObject(process.hProcess, 30000);
        DWORD       code = 1;
        ::GetExitCodeProcess(process.hProcess, &code);
        ::CloseHandle(process.hThread);
        ::CloseHandle(process.hProcess);

        return wait == WAIT_OBJECT_0 && code == 0 && IsDirectory(link_path);
#else
        (void)link_path;
        (void)target_path;
        return false;
#endif
    }

    std::vector<std::string> ListTree(const std::string& directory) noexcept
    {
        std::vector<std::string> entries;

        std::error_code error;
        const std::filesystem::path base(ToNativeSeparators(directory));
        if (!std::filesystem::is_directory(base, error))
        {
            return entries;
        }

        const std::string base_text = TrimTrailingSlash(base.string());
        for (const std::filesystem::directory_entry& entry :
             std::filesystem::recursive_directory_iterator(base, error))
        {
            if (error)
            {
                break;
            }

            std::string full = entry.path().string();
            if (full.size() > base_text.size() + 1)
            {
                full = full.substr(base_text.size() + 1);
            }
            std::replace(full.begin(), full.end(), '\\', '/');

            if (entry.is_directory(error))
            {
                entries.push_back("DIR " + full);
            }
            else if (entry.is_regular_file(error))
            {
                entries.push_back("FILE " + full);
            }
        }

        std::sort(entries.begin(), entries.end());
        return entries;
    }

    std::size_t CountTreeEntries(const std::string& directory) noexcept
    {
        return ListTree(directory).size();
    }

    std::size_t CountRegularFiles(const std::string& directory) noexcept
    {
        std::size_t count = 0;
        for (const std::string& entry : ListTree(directory))
        {
            if (StartsWith(entry, "FILE "))
            {
                ++count;
            }
        }
        return count;
    }

    // ---------------- 摘要与快照 ----------------

    std::string Sha256Hex(std::string_view bytes) noexcept
    {
        return Sha256HexImpl(bytes);
    }

    std::string Sha256OfFile(const std::string& path, bool& is_ok) noexcept
    {
        std::string bytes;
        if (!ReadText(path, bytes) && !Exists(path))
        {
            is_ok = false;
            return std::string();
        }
        is_ok = true;
        return Sha256HexImpl(bytes);
    }

    std::string LastWriteTicks(const std::string& path, bool& is_ok) noexcept
    {
        std::error_code error;
        const std::filesystem::file_time_type time =
            std::filesystem::last_write_time(std::filesystem::path(ToNativeSeparators(path)), error);
        if (error)
        {
            is_ok = false;
            return std::string();
        }
        is_ok = true;
        return std::to_string(static_cast<long long>(time.time_since_epoch().count()));
    }

    Snapshot TakeSnapshot(const CaseRoot& root) noexcept
    {
        Snapshot snapshot;

        const std::string roots[2] = { root.project_root(), root.EnginePath("ID") };
        for (const std::string& tree : roots)
        {
            const std::string base = TrimTrailingSlash(tree);
            for (const std::string& entry : ListTree(tree))
            {
                if (StartsWith(entry, "FILE "))
                {
                    const std::string relative = entry.substr(5);
                    // `IDGenerated/.lock` 是每条命令都会取/放的互斥文件（§1.9），不属于"内容写入"：
                    // 它与任何命令的零写判定无关（锁语义由 N1/F15 单独覆盖），因此不进快照
                    if (relative == "IDGenerated/.lock")
                    {
                        continue;
                    }
                    bool              is_ok    = false;
                    const std::string digest   = Sha256OfFile(JoinPath(base, relative), is_ok);
                    bool              is_time_ok = false;
                    const std::string ticks      = LastWriteTicks(JoinPath(base, relative), is_time_ok);
                    snapshot.entries.push_back("FILE " + relative + " " + (is_ok ? digest : std::string("<unreadable>"))
                                               + " " + (is_time_ok ? ticks : std::string("<notime>")));
                }
                else
                {
                    snapshot.entries.push_back(entry);
                }
            }
        }

        std::sort(snapshot.entries.begin(), snapshot.entries.end());
        return snapshot;
    }

    std::string SnapshotDiff(const Snapshot& before, const Snapshot& after) noexcept
    {
        std::string diff;
        std::size_t index_before = 0;
        std::size_t index_after  = 0;

        while (index_before < before.entries.size() || index_after < after.entries.size())
        {
            if (index_before == before.entries.size())
            {
                diff += "+ " + after.entries[index_after] + "; ";
                ++index_after;
                continue;
            }
            if (index_after == after.entries.size())
            {
                diff += "- " + before.entries[index_before] + "; ";
                ++index_before;
                continue;
            }

            const std::string& left  = before.entries[index_before];
            const std::string& right = after.entries[index_after];
            if (left == right)
            {
                ++index_before;
                ++index_after;
            }
            else if (left < right)
            {
                diff += "- " + left + "; ";
                ++index_before;
            }
            else
            {
                diff += "+ " + right + "; ";
                ++index_after;
            }
        }

        return diff;
    }

    // ---------------- JSON ----------------

    JsonReader::JsonReader(std::string_view text) noexcept
    {
        LoadText(text);
    }

    bool JsonReader::LoadText(std::string_view text) noexcept
    {
        m_document.reset();
        const ID::Json::JsonError error = ID::Json::parse(m_document, text, nullptr);
        m_is_ok                         = error == ID::Json::JsonError::none;
        return m_is_ok;
    }

    bool JsonReader::LoadFile(const std::string& path) noexcept
    {
        std::string text;
        if (!ReadText(path, text))
        {
            m_is_ok = false;
            return false;
        }
        return LoadText(text);
    }

    bool JsonReader::IsOk() const noexcept
    {
        return m_is_ok;
    }

    const ID::Json::JsonValue& JsonReader::Root() const noexcept
    {
        return m_document.root();
    }

    namespace
    {
        const ID::Json::JsonValue* ResolvePath(const ID::Json::JsonValue& node, std::string_view path) noexcept
        {
            const ID::Json::JsonValue* current = &node;
            std::size_t                begin   = 0;

            while (begin <= path.size())
            {
                const std::size_t end  = path.find('.', begin);
                const std::size_t stop = (end == std::string_view::npos) ? path.size() : end;
                const std::string_view token = path.substr(begin, stop - begin);

                if (current == nullptr || token.empty())
                {
                    return nullptr;
                }

                if (current->is_array())
                {
                    std::size_t index = 0;
                    for (const char unit : token)
                    {
                        if (unit < '0' || unit > '9')
                        {
                            return nullptr;
                        }
                        index = index * 10 + static_cast<std::size_t>(unit - '0');
                    }
                    if (index >= current->size())
                    {
                        return nullptr;
                    }
                    current = &(*current)[index];
                }
                else if (current->is_object())
                {
                    current = current->find(token);
                }
                else
                {
                    return nullptr;
                }

                if (end == std::string_view::npos)
                {
                    break;
                }
                begin = end + 1;
            }

            return current;
        }
    } // namespace

    const ID::Json::JsonValue* JsonReader::Find(std::string_view dotted_path) const noexcept
    {
        if (!m_is_ok)
        {
            return nullptr;
        }

        if (dotted_path.empty())
        {
            return &m_document.root();
        }

        return ResolvePath(m_document.root(), dotted_path);
    }

    std::string JsonReader::String(std::string_view dotted_path) const noexcept
    {
        const ID::Json::JsonValue* node = Find(dotted_path);
        if (node == nullptr || !node->is_string())
        {
            return std::string();
        }
        return std::string(node->as_string());
    }

    bool JsonReader::Bool(std::string_view dotted_path, bool fallback) const noexcept
    {
        const ID::Json::JsonValue* node = Find(dotted_path);
        if (node == nullptr || !node->is_bool())
        {
            return fallback;
        }
        return node->as_bool();
    }

    long long JsonReader::Int(std::string_view dotted_path, long long fallback) const noexcept
    {
        const ID::Json::JsonValue* node = Find(dotted_path);
        if (node == nullptr || !node->is_int())
        {
            return fallback;
        }
        return static_cast<long long>(node->as_int64());
    }

    std::size_t JsonReader::Count(std::string_view dotted_path) const noexcept
    {
        const ID::Json::JsonValue* node = Find(dotted_path);
        if (node == nullptr)
        {
            return 0;
        }
        return node->size();
    }

    // ---------------- 夹具 ----------------

    bool WritePlugin(const CaseRoot& root, const PluginSpec& spec) noexcept
    {
        if (spec.name.empty())
        {
            return false;
        }

        const std::string plugin_dir = JoinPath(root.plugin_root(), spec.name);

        std::string manifest;
        if (!spec.manifest_override.empty())
        {
            manifest = spec.manifest_override;
        }
        else
        {
            manifest = "{\n    \"name\": \"" + spec.name + "\",\n"
                       "    \"dependencies\": " + spec.dependencies_json + ",\n"
                       "    \"requirements\": " + spec.requirements_json + ",\n"
                       "    \"header_dirs\": " + spec.header_dirs_json + ",\n"
                       "    \"common_headers\": " + spec.common_headers_json + ",\n"
                       "    \"source_dirs\": " + spec.source_dirs_json + ",\n"
                       "    \"vendor_dirs\": " + spec.vendor_dirs_json + ",\n"
                       "    \"shared_vendors\": " + spec.shared_vendors_json + ",\n"
                       "    \"files\": " + spec.files_json + ",\n"
                       "    \"options\": " + spec.options_json + "\n}\n";
        }

        bool is_ok = true;
        if (spec.has_manifest)
        {
            is_ok = WriteText(JoinPath(plugin_dir, "plugin_config.json"), manifest) && is_ok;
        }

        // 标识符净化：插件名可能含 '.'（如 ID.Sample），只用于合成代码里的函数名
        std::string symbol;
        for (const char unit : spec.name)
        {
            symbol.push_back((unit >= 'a' && unit <= 'z') || (unit >= 'A' && unit <= 'Z') || (unit >= '0' && unit <= '9')
                                 ? unit
                                 : '_');
        }

        if (spec.has_include)
        {
            const std::string header = "#pragma once\n\nnamespace ID::Fixture\n{\n    int " + symbol
                                     + "_value() noexcept;\n}\n";
            is_ok = WriteText(JoinPath(plugin_dir, "include/" + symbol + ".hpp"), header) && is_ok;
        }

        if (spec.has_src)
        {
            const std::string source = "#include \"" + symbol + ".hpp\"\n\nnamespace ID::Fixture\n{\n    int " + symbol
                                     + "_value() noexcept\n    {\n        return 1;\n    }\n}\n";
            is_ok = WriteText(JoinPath(plugin_dir, "src/" + symbol + ".cpp"), source) && is_ok;
        }

        if (spec.has_readme)
        {
            is_ok = WriteText(JoinPath(plugin_dir, "README.md"), "# " + spec.name + "\n\n测试夹具插件。\n") && is_ok;
        }

        if (spec.has_license)
        {
            is_ok = WriteText(JoinPath(plugin_dir, "LICENSE"), spec.name + " fixture license\n") && is_ok;
        }

        return is_ok;
    }

    std::string RealEngineRoot() noexcept
    {
        const std::string tool(BENCHMARK_ID_EXE);
        if (tool.empty())
        {
            return std::string();
        }

        std::filesystem::path binary(tool);
        const std::filesystem::path bin_dir   = binary.parent_path();
        const std::filesystem::path engine    = bin_dir.parent_path();
        if (engine.empty())
        {
            return std::string();
        }
        return engine.string();
    }

    bool CopyRealPlugin(const CaseRoot& root, std::string_view name) noexcept
    {
        const std::string engine = RealEngineRoot();
        if (engine.empty())
        {
            return false;
        }

        const std::string source = JoinPath(JoinPath(engine, "IDPlugin"), name);
        if (!IsDirectory(source))
        {
            return false;
        }

        return CopyTree(source, JoinPath(root.plugin_root(), name));
    }

    bool CopyRealSharedAssets(const CaseRoot& root) noexcept
    {
        const std::string engine = RealEngineRoot();
        if (engine.empty())
        {
            return false;
        }

        bool is_ok = CopyTree(JoinPath(JoinPath(engine, "IDPlugin"), "Common"), JoinPath(root.plugin_root(), "Common"));

        const std::string vendor_source = JoinPath(JoinPath(engine, "IDPlugin"), "Vendor");
        if (IsDirectory(vendor_source))
        {
            is_ok = CopyTree(vendor_source, JoinPath(root.plugin_root(), "Vendor")) && is_ok;
        }
        return is_ok;
    }

    bool WritePluginsJson(const CaseRoot& root, std::string_view project_name, std::string_view plugins_json) noexcept
    {
        const std::string text = "{\n    \"name\": \"" + std::string(project_name) + "\",\n    \"plugins\": "
                               + std::string(plugins_json) + "\n}\n";
        return WriteText(root.ProjectPath("plugins.json"), text);
    }

    std::string ProjectRecordPath(const CaseRoot& root, std::string_view file) noexcept
    {
        return root.ProjectPath("IDGenerated/" + std::string(file));
    }

    bool ReadProjectRecord(const CaseRoot& root, std::string_view file, std::string& out) noexcept
    {
        return ReadText(ProjectRecordPath(root, file), out);
    }

    bool ReadResolved(const CaseRoot& root, JsonReader& reader) noexcept
    {
        return reader.LoadFile(ProjectRecordPath(root, "resolved.json"));
    }

    bool ReadManifest(const CaseRoot& root, JsonReader& reader) noexcept
    {
        return reader.LoadFile(ProjectRecordPath(root, "manifest.json"));
    }

    bool ReadBuildRecords(const CaseRoot& root, JsonReader& reader) noexcept
    {
        return reader.LoadFile(ProjectRecordPath(root, "build.json"));
    }

    bool FindRegistryEntry(const CaseRoot& root, std::string_view name, bool& is_initialized,
                           std::size_t& total) noexcept
    {
        is_initialized = false;
        total          = 0;

        JsonReader reader;
        if (!reader.LoadFile(RegistryPath(root)))
        {
            return false;
        }

        const ID::Json::JsonValue& registry = reader.Root();
        total                               = registry.size();
        for (std::size_t index = 0; index < registry.size(); ++index)
        {
            const ID::Json::JsonValue& entry = registry[index];
            const ID::Json::JsonValue* named = entry.find("name");
            if (named == nullptr || !named->is_string() || named->as_string() != name)
            {
                continue;
            }

            const ID::Json::JsonValue* marker = entry.find("is_initialized");
            is_initialized                   = marker != nullptr && marker->is_bool() && marker->as_bool();
            return true;
        }
        return false;
    }

    bool HasRegistryEntry(const CaseRoot& root, std::string_view name) noexcept
    {
        bool        is_initialized = false;
        std::size_t total          = 0;
        return FindRegistryEntry(root, name, is_initialized, total);
    }

    std::string RegistryPath(const CaseRoot& root) noexcept
    {
        return root.EnginePath("ID/project.json");
    }

    void AppendMingwProfile(std::vector<std::string>& arguments) noexcept
    {
        arguments.emplace_back("--generator");
        arguments.emplace_back("MinGW Makefiles");
        arguments.emplace_back("--toolchain");
        arguments.emplace_back("mingw");
        arguments.emplace_back("--arch");
        arguments.emplace_back("x64");
    }

    std::string ArtifactPath(const CaseRoot& root, std::string_view toolchain, std::string_view arch,
                             std::string_view config, std::string_view exe_name) noexcept
    {
        return root.ProjectPath("bin/" + std::string(toolchain) + "/" + std::string(arch) + "/" + std::string(config)
                                + "/" + std::string(exe_name));
    }
} // namespace ID::Bench::IdProject
