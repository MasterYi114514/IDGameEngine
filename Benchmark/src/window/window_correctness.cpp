#include "window/window_domain.hpp"

#include <algorithm>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <type_traits>

#include "IDWindow.hpp"
#include "IDWindow/ContextInfo.hpp"
#include "IDWindow/Event/EventContext.hpp"
#include "IDWindow/Event/EventData.hpp"
#include "IDWindow/Event/EventManager.hpp"
#include "IDWindow/Event/EventType.hpp"
#include "IDWindow/KeyCode/KeyCodes.hpp"
#include "IDWindow/NativeWindowInfo.hpp"
#include "IDWindow/ProcAddress.hpp"
#include "IDWindow/VulkanWsiRequirements.hpp"
#include "IDWindow/Window.hpp"
#include "IDWindow/WindowPool.hpp"
#include "IDWindow/WindowStatus.hpp"

#include "benchmark_support.hpp"

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
 *  IDWindow 正确性用例：按公开头与 README 写明的契约断言，不按实现猜测。
 *
 *  取证方式：
 *  - 编译期事实（句柄宽度、访问控制）在构建期由 `static_assert` 与负例 TU 判定；负例 TU 不在
 *    构建图里（`.cpp.in` 不被 `src` 下的 `.cpp` 通配收集），由本域在运行期用**同一编译器与同一 include 根**
 *    执行 `-fsyntax-only` 取证，并先跑一个合法对照 TU 证明这些失败有鉴别力（methodology §4）。
 *  - 事件一律由平台回调栈产生（`Window::set_size` → WM_SIZE → 该窗口自己的回调表），
 *    再用 Win32 `SendMessage` 做输入类事件的注入尝试；注入失败不判失败，按"触发手段不可得"记 Skip。
 *  - 窗口全程 `is_visible = false`（任务卡 §2）。唯一的例外在状态位用例的 Visible 置位子项：
 *    契约的 Visible 位只有真正 `show()` 后才置位，该子项做一次 show→读状态→hide，
 *    可见时长在报告里如实给出，随后立即隐藏，不残留窗口。
 *  - 每个用例自包含（自己 shutdown 到未初始化、自己 init），不依赖用例顺序。
 *  - 判定用 `Check` 收集并继续，不用断言宏终止进程。
 */

// ---- 编译期契约断言（构建期证据：本 TU 编译通过即成立） ----

static_assert(sizeof(ID::Window::WindowUINT) == 2, "契约：WindowUINT 必须是 2 字节（std::uint16_t），句柄宽度不加宽");
static_assert(sizeof(ID::Window::WindowHandle) == 2, "契约：WindowHandle 只有一个句柄字，默认构造得到无效句柄");
static_assert(!std::is_default_constructible_v<ID::Window::Window>, "契约：Window 不允许默认构造，只能由 WindowPool 创建");
static_assert(!std::is_destructible_v<ID::Window::Window>, "契约：Window 必须不可析构，析构权只归 WindowPool");
static_assert(!std::is_copy_constructible_v<ID::Window::Window>, "契约：Window 不可复制（地址稳定与句柄唯一）");
static_assert(!std::is_move_constructible_v<ID::Window::Window>, "契约：Window 不可移动（移动会破坏地址稳定）");
static_assert(sizeof(ID::Window::WindowStatus) == 1, "契约：WindowStatus 只有一个位集合字（单字节，用掉 7 位）");
static_assert(ID_WINDOW_INTERFACE_VERSION == 2, "契约：接口版本为 2（README 第 8 节的 1→2 破坏性变更）");

namespace
{
    namespace W = ID::Window;

    using ID::Bench::Check;
    using ID::Bench::FormatDouble;
    using ID::Bench::NowNs;
    using ID::Bench::ReportLine;
    using ID::Bench::Skip;
    using ID::Bench::TempPath;
    using ID::Bench::Uncovered;

    using W::WindowHandle;
    using W::WindowStatusEnum;

    constexpr std::uint32_t kEventTypeCount = 24;
    constexpr int           kEventSlots     = 3;
    constexpr std::size_t   kPoolCapacity   = 64;

    // 事件类型名表：下标与 `Event::EventType` 的数值逐一对应（只用于报告与 Skip 行）
    const char* const kEventTypeNames[kEventTypeCount] = {
        "None",
        "WindowClose", "WindowResize", "WindowFocus", "WindowLostFocus", "WindowMoved",
        "KeyPressed", "KeyReleased", "KeyRepeat",
        "MouseButtonPressed", "MouseButtonReleased", "MouseMoved", "MouseScrolled",
        "WindowFramebufferResize", "WindowContentScaleChanged", "WindowMinimized", "WindowRestored",
        "WindowMaximized", "WindowUnmaximized", "WindowRefresh", "WindowCursorEntered", "WindowCursorLeft",
        "TextInput", "FileDrop",
    };

    // 未观测到的事件类型按类别给出"为什么触发不到"（逐类一条 Skip 行，行内列出该类的全部类型名）
    struct TriggerCategory
    {
        const char* m_reason;
        std::uint32_t m_types[8];
        int m_count;
    };

    constexpr TriggerCategory kUntriggerableCategories[] = {
        { "高优先级关闭（Alt+F4/系统菜单）才会产生 WindowClose；`request_close()` 只置位关闭标志、不派发"
          "（README §7），GLFW 公开 API 也没有「请求关闭」的注入入口，隐藏窗口更不会收到系统关闭命令",
          { 1 }, 1 },
        { "焦点变化只在真实前台切换时产生；本域全程隐藏窗口且不调用 OS 焦点 API，"
          "注入 WM_SETFOCUS/WM_KILLFOCUS 后仍未观测到事件（见 window.events.injected_messages_only_to_target 行）",
          { 3, 4 }, 2 },
        { "鼠标类事件需要真实指针进入窗口内容区；隐藏窗口不在桌面上，无坐标可进入，"
          "GLFW 公开 API 也没有「合成鼠标输入」的入口",
          { 9, 10, 11, 12 }, 4 },
        { "窗口系统状态类事件需要真实显示/最小化/最大化/DPI 变化或光标进入内容区；本域全程隐藏窗口，"
          "且契约没有把 iconify/maximize 暴露为窗口操作（README §2.3 只有 show/hide/focus/lose_focus）",
          { 14, 15, 16, 17, 18, 19, 20, 21 }, 8 },
        { "文本与拖放来自输入法提交与 Explorer 拖放，需要真实前台窗口与桌面交互；"
          "隐藏窗口不接收这两类输入，插件也没有对应的注入入口",
          { 22, 23 }, 2 },
    };

    struct Tally
    {
        std::uint32_t m_total                    = 0;
        std::uint32_t m_by_type[kEventTypeCount] = {};
    };

    struct Slot
    {
        Tally        m_tally{};
        WindowHandle m_handle{};
    };

    Slot g_slots[kEventSlots] = {};

    void record(Slot& slot, std::uint32_t type) noexcept
    {
        ++slot.m_tally.m_total;
        if (type < kEventTypeCount)
        {
            ++slot.m_tally.m_by_type[type];
        }
    }

    std::string last_error_text() noexcept
    {
        const char* const text = W::last_error();
        return text == nullptr ? std::string() : std::string(text);
    }

    bool error_has(const char* needle) noexcept
    {
        const char* const text = W::last_error();
        return text != nullptr && std::strstr(text, needle) != nullptr;
    }

    W::WindowDesc make_desc(const char* title, bool visible) noexcept
    {
        W::WindowDesc desc{};
        desc.title      = title;
        desc.size       = W::WindowSize{ 320, 240 };
        desc.is_visible = visible;
        desc.render_api = W::RenderApi::None;
        return desc;
    }

    // ---- 16 个回调：统一记录"收到事件的窗口序号 / EventType" ----

    void on_window_close(void* user, W::Window&, const W::Event::WindowCloseEvent&) noexcept
    {
        record(*static_cast<Slot*>(user), 1);
    }

    void on_window_size(void* user, W::Window&, const W::Event::WindowSizeEvent&) noexcept
    {
        record(*static_cast<Slot*>(user), 2);
    }

    void on_window_focus(void* user, W::Window&, const W::Event::WindowFocusEvent& event) noexcept
    {
        record(*static_cast<Slot*>(user), event.is_focused ? 3u : 4u);
    }

    void on_window_moved(void* user, W::Window&, const W::Event::WindowPosEvent&) noexcept
    {
        record(*static_cast<Slot*>(user), 5);
    }

    void on_key(void* user, W::Window&, const W::Event::KeyEvent& event) noexcept
    {
        std::uint32_t type = 6;
        if (event.action == W::Event::KeyAction::Released)
        {
            type = 7;
        }
        else if (event.action == W::Event::KeyAction::Repeat)
        {
            type = 8;
        }
        record(*static_cast<Slot*>(user), type);
    }

    void on_mouse_button(void* user, W::Window&, const W::Event::MouseButtonEvent& event) noexcept
    {
        record(*static_cast<Slot*>(user), event.action == W::Event::KeyAction::Pressed ? 9u : 10u);
    }

    void on_cursor_pos(void* user, W::Window&, const W::Event::CursorPosEvent&) noexcept
    {
        record(*static_cast<Slot*>(user), 11);
    }

    void on_scroll(void* user, W::Window&, const W::Event::ScrollEvent&) noexcept
    {
        record(*static_cast<Slot*>(user), 12);
    }

    void on_framebuffer_size(void* user, W::Window&, const W::Event::WindowSizeEvent&) noexcept
    {
        record(*static_cast<Slot*>(user), 13);
    }

    void on_content_scale(void* user, W::Window&, const W::Event::ContentScaleEvent&) noexcept
    {
        record(*static_cast<Slot*>(user), 14);
    }

    void on_iconify(void* user, W::Window&, const W::Event::WindowIconifyEvent& event) noexcept
    {
        record(*static_cast<Slot*>(user), event.is_iconified ? 15u : 16u);
    }

    void on_maximize(void* user, W::Window&, const W::Event::WindowMaximizeEvent& event) noexcept
    {
        record(*static_cast<Slot*>(user), event.is_maximized ? 17u : 18u);
    }

    void on_refresh(void* user, W::Window&, const W::Event::WindowRefreshEvent&) noexcept
    {
        record(*static_cast<Slot*>(user), 19);
    }

    void on_cursor_enter(void* user, W::Window&, const W::Event::CursorEnterEvent& event) noexcept
    {
        record(*static_cast<Slot*>(user), event.is_entered ? 20u : 21u);
    }

    void on_char(void* user, W::Window&, const W::Event::CharEvent&) noexcept
    {
        record(*static_cast<Slot*>(user), 22);
    }

    void on_file_drop(void* user, W::Window&, const W::Event::FileDropEvent&) noexcept
    {
        record(*static_cast<Slot*>(user), 23);
    }

    void register_all(W::Window& window, Slot& slot) noexcept
    {
        window.set_key_callback(&on_key, &slot);
        window.set_mouse_button_callback(&on_mouse_button, &slot);
        window.set_cursor_pos_callback(&on_cursor_pos, &slot);
        window.set_char_callback(&on_char, &slot);
        window.set_window_close_callback(&on_window_close, &slot);
        window.set_window_size_callback(&on_window_size, &slot);
        window.set_framebuffer_size_callback(&on_framebuffer_size, &slot);
        window.set_window_focus_callback(&on_window_focus, &slot);
        window.set_window_iconify_callback(&on_iconify, &slot);
        window.set_window_maximize_callback(&on_maximize, &slot);
        window.set_window_refresh_callback(&on_refresh, &slot);
        window.set_cursor_enter_callback(&on_cursor_enter, &slot);
        window.set_scroll_callback(&on_scroll, &slot);
        window.set_file_drop_callback(&on_file_drop, &slot);
        window.set_window_pos_callback(&on_window_moved, &slot);
        window.set_content_scale_callback(&on_content_scale, &slot);
    }

    void unregister_all(W::Window& window) noexcept
    {
        window.set_key_callback(nullptr, nullptr);
        window.set_mouse_button_callback(nullptr, nullptr);
        window.set_cursor_pos_callback(nullptr, nullptr);
        window.set_char_callback(nullptr, nullptr);
        window.set_window_close_callback(nullptr, nullptr);
        window.set_window_size_callback(nullptr, nullptr);
        window.set_framebuffer_size_callback(nullptr, nullptr);
        window.set_window_focus_callback(nullptr, nullptr);
        window.set_window_iconify_callback(nullptr, nullptr);
        window.set_window_maximize_callback(nullptr, nullptr);
        window.set_window_refresh_callback(nullptr, nullptr);
        window.set_cursor_enter_callback(nullptr, nullptr);
        window.set_scroll_callback(nullptr, nullptr);
        window.set_file_drop_callback(nullptr, nullptr);
        window.set_window_pos_callback(nullptr, nullptr);
        window.set_content_scale_callback(nullptr, nullptr);
    }

    // ---- 域内前置：回到"未初始化"再 init，失败即记录并跳过后续断言 ----

    bool begin_case(const char* check_name) noexcept
    {
        W::shutdown();
        W::shutdown();      // 幂等，确保回到未初始化

        if (!W::init())
        {
            Check(false, check_name, std::string("前置失败：ID::Window::init() 返回 false，last_error=") + last_error_text());
            return false;
        }
        return true;
    }

    // ================= 用例 1：链接与导出面 =================

    void case_link_and_export() noexcept
    {
        W::shutdown();

        const char* const version = W::version_string();
        Check(version != nullptr && std::strstr(version, "IDWindow 0.1 (GLFW 3.5.1)") != nullptr,
              "window.link.version_string",
              std::string("契约：version_string() 返回编译期拼接的静态串 \"IDWindow 0.1 (GLFW 3.5.1)\"；实际=")
                  + (version == nullptr ? "<nullptr>" : version));
        Check(version != nullptr && version == W::version_string(),
              "window.link.version_pointer_stable",
              "契约：同一模块加载周期内地址与内容不变（不分配、不格式化、不加锁）");

        // 链接与导出面：不看 DLL 文件名（MinGW 是 libIDWindow.dll、MSVC 是 IDWindow.dll），
        // 而是查"version_string 的代码段落在哪个模块里"——STATIC 必须落在 exe，SHARED 必须落在 DLL
        {
            HMODULE owner = nullptr;
            const BOOL found = ::GetModuleHandleExA(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCSTR>(reinterpret_cast<void*>(&W::version_string)), &owner);
            const bool code_in_exe = (found != FALSE) && (owner == ::GetModuleHandleA(nullptr));

#if defined(ID_WINDOW_STATIC)
            Check(found != FALSE && code_in_exe, "window.link.form_static_code_in_exe",
                  "STATIC 形态：ID_WINDOW_STATIC 由被测目标传递，version_string 的代码段必须落在可执行文件内"
                  "（运行期不依赖 IDWindow 的 DLL）");
#else
            Check(found != FALSE && !code_in_exe, "window.link.form_shared_code_in_dll",
                  "SHARED 形态：公开头按 dllimport 声明，version_string 的代码段必须落在 IDWindow 的 DLL 内"
                  "（DLL 由构建 POST_BUILD 放到 exe 旁）");
            char module_path[MAX_PATH] = {};
            if (found != FALSE && ::GetModuleFileNameA(owner, module_path, sizeof(module_path)) != 0)
            {
                ReportLine(std::string("    idwindow module        : ") + module_path);
            }
#endif
        }

        // 最小消费闭环：init → create → get_window → 查询 → destroy → shutdown（只用公开头）
        const bool initialized = W::init();
        Check(initialized, "window.link.minimal_consumer_init",
              std::string("最小消费：ID::Window::init() 必须成功（成功路径不清空既有错误文本，本条不回显陈旧文本）"));

        if (initialized)
        {
            const WindowHandle handle = W::WindowPool::create_window(make_desc("bench-link", false));
            Check(handle.is_valid(), "window.link.minimal_consumer_create",
                  std::string("最小消费：create_window 返回有效句柄（成功路径不清空既有错误文本，本条不回显陈旧文本）"));

            if (handle.is_valid())
            {
                W::Window& window = W::WindowPool::get_window(handle);
                const W::WindowSize size = window.get_size();
                Check(size.width == 320 && size.height == 240, "window.link.minimal_consumer_query",
                      std::string("最小消费：get_size 返回创建时请求的逻辑尺寸 320x240；实际=")
                          + std::to_string(size.width) + "x" + std::to_string(size.height));
                W::WindowPool::destroy_window(handle);
            }
        }

        W::shutdown();
        Check(!W::is_initialized(), "window.link.minimal_consumer_shutdown",
              "最小消费：shutdown 之后 is_initialized() 必须为 false");
    }

    // ================= 用例 2：公开头自包含 =================

    void case_public_headers() noexcept
    {
        // 构建期证据：src/window/headers/probe_*.cpp 每个 TU 只包含一个公开头，随后包含依赖守卫。
        // 这里在运行期核对"探针文件的形状"（数量、单包含、守卫包含），构建成功则证明自包含与依赖隔离。
        const std::string dir = std::string(BENCHMARK_WINDOW_SRC_DIR) + "/window/headers";

        std::size_t probe_count = 0;
        std::size_t single_include_violations = 0;
        std::size_t guard_missing = 0;

        std::error_code error;
        for (const auto& entry : std::filesystem::directory_iterator(dir, error))
        {
            if (!entry.is_regular_file())
            {
                continue;
            }
            if (entry.path().extension() != ".cpp")
            {
                continue;
            }

            ++probe_count;

            std::string text;
            {
                std::FILE* const file = std::fopen(entry.path().string().c_str(), "rb");
                if (file != nullptr)
                {
                    char buffer[2048];
                    std::size_t read = 0;
                    while ((read = std::fread(buffer, 1, sizeof(buffer), file)) > 0)
                    {
                        text.append(buffer, read);
                    }
                    std::fclose(file);
                }
            }

            // 断言形状：恰好一行 `#include "IDWindow...`，且包含依赖守卫头
            std::size_t window_include_lines = 0;
            std::size_t pos = 0;
            while ((pos = text.find("#include \"IDWindow", pos)) != std::string::npos)
            {
                ++window_include_lines;
                pos += 8;
            }

            if (window_include_lines != 1)
            {
                ++single_include_violations;
            }
            if (text.find("window/window_header_probe_check.hpp") == std::string::npos)
            {
                ++guard_missing;
            }
        }

        Check(probe_count == 27, "window.headers.probe_translation_units",
              std::string("契约：27 个公开头各有一个独立探针 TU；实际探针数=") + std::to_string(probe_count));
        Check(single_include_violations == 0, "window.headers.single_public_include_per_tu",
              std::string("每个探针 TU 只包含一个 IDWindow 公开头（保证测的是「单独包含即能编译」）；违规 TU 数=")
                  + std::to_string(single_include_violations));
        Check(guard_missing == 0, "window.headers.dependency_guard_present",
              std::string("每个探针 TU 都包含依赖守卫头；缺失数=") + std::to_string(guard_missing));
        Check(true, "window.headers.self_contained_and_isolated",
              "构建期判定：27 个探针 TU 全部编译通过 → 每个公开头单独包含即可编译，且没有把 GLFW/"
              "windows.h/Vulkan/IDLog 头带进消费者（守卫头以 #error 负断言，构建日志归档 out/verify/T25/）");
        ReportLine("    headers checked        : 27（IDWindow.hpp + IDWindowCore.hpp + IDWindow/**.hpp 25 个）"
                   "；守卫宏 = GLFW_VERSION_MAJOR / _WINDOWS_ / VULKAN_H_ / ID_LOG_API");
    }

    // ================= 用例 3：访问控制（预期不可编译） =================

    struct CompileOutcome
    {
        bool        m_spawned   = false;
        int         m_exit_code = -1;
        std::string m_command;
        std::string m_output;
    };

    /**
     *  @brief 把一段文本整体写到文件（负例取证日志用；失败静默，不改变用例判定）
     */
    void write_log_file(const std::string& path, std::string_view text) noexcept
    {
        std::FILE* const file = std::fopen(path.c_str(), "wb");
        if (file == nullptr)
        {
            return;
        }
        std::fwrite(text.data(), 1, text.size(), file);
        std::fclose(file);
    }

    /**
     *  @brief 用构建期注入的同一编译器、同一 include 根对单个 TU 做语法级编译
     *  @param source_path 被测 TU 的绝对路径
     *  @return 是否成功启动、编译器退出码、合并后的诊断输出
     *  @note 只做 `-fsyntax-only` / `/Zs`：不产生目标文件，不进入官方可执行文件；
     *        输出与退出码是"预期不可编译"用例的唯一证据
     */
    CompileOutcome run_compiler(const std::string& source_path) noexcept
    {
        CompileOutcome outcome;

#if defined(_WIN32)
        std::string command = "\"";
        command += BENCHMARK_CXX_COMPILER;
        command += "\" ";

        if (std::strcmp(BENCHMARK_CXX_FAMILY, "MSVC") == 0)
        {
            command += "/nologo /std:c++20 /Zs /EHsc /utf-8 /I\"";
            command += BENCHMARK_IDWINDOW_INCLUDE;
            command += "\" /I\"";
            command += BENCHMARK_COMMON_INCLUDE;
            command += "\" /Tp\"";
        }
        else
        {
            command += "-std=c++20 -fsyntax-only -x c++ -I\"";
            command += BENCHMARK_IDWINDOW_INCLUDE;
            command += "\" -I\"";
            command += BENCHMARK_COMMON_INCLUDE;
            command += "\" \"";
        }

        command += source_path;
        command += "\" 2>&1";

        // `_popen` 走 `cmd.exe /c`；当整条命令行以引号开头时，cmd 会自己剥掉首尾引号，把可执行文件路径
        // 拆坏（实测报 "The filename, directory name, or volume label syntax is incorrect."）。
        // 按 cmd 的规则再包一层引号，命令行才是原本要执行的那条。
        const std::string wrapped = "\"" + command + "\"";
        outcome.m_command = wrapped;

        std::FILE* const pipe = _popen(wrapped.c_str(), "r");
        if (pipe == nullptr)
        {
            return outcome;
        }

        outcome.m_spawned = true;
        char buffer[1024];
        while (std::fgets(buffer, static_cast<int>(sizeof(buffer)), pipe) != nullptr)
        {
            outcome.m_output += buffer;
        }
        outcome.m_exit_code = _pclose(pipe);
#else
        (void)source_path;
        outcome.m_output = "非 Windows：编译器级负例取证尚未实现";
#endif
        return outcome;
    }

    struct AccessNegativeCase
    {
        const char* m_file;
        const char* m_check_name;
        const char* m_marker;
        const char* m_marker_fallback;
        const char* m_detail;
    };

    // 类别标记取被测公开头的拒绝点（诊断里必然出现的成员全名），不另造一套类别名。
    // `m_marker_fallback` 兜住"私有 + = delete"被编译器报成 deleted 的措辞差异。
    constexpr AccessNegativeCase kAccessNegativeCases[] = {
        { "window_negative_construct.cpp.in", "window.access.external_construct",
          "ID::Window::Window::Window", "deleted",
          "契约：Window 构造函数私有且 = delete，外部 TU 构造必须编译失败" },
        { "window_negative_destruct.cpp.in", "window.access.external_destruct",
          "ID::Window::Window::~Window", "private",
          "契约：Window 析构函数私有，外部 TU 析构必须编译失败" },
        { "window_negative_destroy.cpp.in", "window.access.private_destroy",
          "ID::Window::Window::destroy", "private",
          "契约：destroy() 是私有生命周期入口，只由 WindowPool 调用" },
        { "window_negative_open_platform.cpp.in", "window.access.private_open_platform",
          "ID::Window::Window::open_platform", "private",
          "契约：open_platform() 是私有创建入口，只能经 WindowPool 触发" },
        { "window_negative_on_event.cpp.in", "window.access.private_on_event",
          "ID::Window::Window::on_event", "private",
          "契约：on_event() 是私有派发入口，事件只从窗口自己的平台回调栈进入" },
        { "window_negative_pool_construct.cpp.in", "window.access.pool_not_instantiable",
          "ID::Window::WindowPool::WindowPool", "deleted",
          "契约：WindowPool 只提供静态接口，不允许实例化" },
    };

    void case_access_control() noexcept
    {
        const std::string dir = std::string(BENCHMARK_WINDOW_SRC_DIR) + "/window/negative/";

        // 合法对照：同编译器、同选项、同 include 根下，只用公开契约的 TU 必须编译通过；
        // 没有这条对照，"编译失败"就无法区分"契约拒绝"与"环境坏了"（methodology §4）
        const CompileOutcome legal = run_compiler(dir + "window_access_legal_control.cpp.in");
        {
            const std::string log_path = TempPath("window-access-legal-control.log");
            write_log_file(log_path, std::string("command: ") + legal.m_command + "\nexit="
                                        + std::to_string(legal.m_exit_code) + "\n" + legal.m_output);
            Check(legal.m_spawned && legal.m_exit_code == 0, "window.access.legal_control_compiles",
                  std::string("同环境同选项的合法对照 TU 必须编译通过（退出码 0）；spawned=")
                      + (legal.m_spawned ? "1" : "0") + " exit=" + std::to_string(legal.m_exit_code)
                      + " log=" + log_path);
        }

        for (const AccessNegativeCase& item : kAccessNegativeCases)
        {
            const CompileOutcome outcome = run_compiler(dir + item.m_file);

            std::string log_name = "window-access-";
            log_name += item.m_file;
            log_name += ".log";
            const std::string log_path = TempPath(log_name);
            write_log_file(log_path, std::string("command: ") + outcome.m_command + "\nexit="
                                        + std::to_string(outcome.m_exit_code) + "\n" + outcome.m_output);

            const bool has_error  = outcome.m_output.find("error") != std::string::npos;
            const bool marker_hit = outcome.m_output.find(item.m_marker) != std::string::npos
                                 || (item.m_marker_fallback != nullptr
                                     && outcome.m_output.find(item.m_marker_fallback) != std::string::npos);
            const bool ok = legal.m_exit_code == 0 && outcome.m_spawned
                         && outcome.m_exit_code != 0 && has_error && marker_hit;

            Check(ok, item.m_check_name,
                  std::string(item.m_detail) + "；实测 exit=" + std::to_string(outcome.m_exit_code)
                      + " error_line=" + (has_error ? "1" : "0")
                      + " marker=" + item.m_marker + " hit=" + (marker_hit ? "1" : "0")
                      + " log=" + log_path);
        }

        ReportLine(std::string("    access negative        : 编译器=") + BENCHMARK_CXX_COMPILER
                   + "；选项=-std=c++20 -fsyntax-only（MSVC 为 /std:c++20 /Zs）；TU 目录=" + dir
                   + "；每次取证的命令与完整诊断落盘到本变体 tmp 目录（window-access-*.log）");
    }

    // ================= 用例 4：生命周期 =================

    void case_lifecycle() noexcept
    {
        W::shutdown();
        Check(!W::is_initialized(), "window.lifecycle.uninitialized_before_init",
              "契约：init() 之前 is_initialized() 为 false");

        Check(W::init(), "window.lifecycle.init_first",
              std::string("契约：首次 init() 成功（成功路径不清空既有错误文本，本条不回显陈旧文本）"));
        Check(W::is_initialized(), "window.lifecycle.initialized_after_init",
              "契约：init() 成功后 is_initialized() 为 true");

        // 幂等 init 不清空既有错误文本：先制造一条错误文本，再 init 一次
        const WindowHandle rejected = W::WindowPool::create_window(W::WindowDesc{ nullptr, W::WindowSize{ 320, 240 } });
        const std::string error_before(W::last_error());
        const bool idempotent = W::init();
        Check(!rejected.is_valid() && !error_before.empty() && idempotent && error_before == W::last_error(),
              "window.lifecycle.init_idempotent",
              std::string("契约：已初始化时 init() 直接返回 true，不重复初始化也不清空 last_error()；"
                          "error_before_empty=") + (error_before.empty() ? "1" : "0"));

        W::shutdown();
        Check(!W::is_initialized(), "window.lifecycle.shutdown", "契约：shutdown 之后 is_initialized() 为 false");
        W::shutdown();
        Check(!W::is_initialized(), "window.lifecycle.shutdown_idempotent",
              "契约：未初始化时 shutdown() 是无操作");

        Check(W::init(), "window.lifecycle.reinit_after_shutdown",
              std::string("契约：shutdown 之后可以再次 init()（成功路径不清空既有错误文本，本条不回显陈旧文本）"));
        const WindowHandle handle = W::WindowPool::create_window(make_desc("bench-reinit", false));
        Check(handle.is_valid(), "window.lifecycle.reinit_window_created",
              std::string("重 init 之后必须能重新创建窗口（成功路径不清空既有错误文本，本条不回显陈旧文本）"));
        if (handle.is_valid())
        {
            W::WindowPool::destroy_window(handle);
        }
        W::shutdown();
    }

    // ================= 用例 5：句柄与容量 =================

    void case_handles() noexcept
    {
        if (!begin_case("window.handle.init"))
        {
            return;
        }

        const WindowHandle none{};
        Check(!none.is_valid() && none.get_id() == static_cast<W::WindowUINT>(-1),
              "window.handle.default_invalid",
              "契约：默认构造得到无效句柄（底层最大值表示无效）");

        // 64 槽：前 64 次创建必须成功，第 65 次必须返回无效句柄并留下错误文本
        WindowHandle handles[kPoolCapacity + 1] = {};
        char title[32] = {};
        std::size_t created = 0;
        for (; created <= kPoolCapacity; ++created)
        {
            std::snprintf(title, sizeof(title), "bench-slot-%u", static_cast<unsigned>(created));
            const WindowHandle handle = W::WindowPool::create_window(make_desc(title, false));
            handles[created] = handle;
            if (!handle.is_valid())
            {
                break;
            }
        }

        Check(created == kPoolCapacity, "window.handle.slot_capacity_64",
              std::string("契约：固定 64 槽，前 64 次 create_window 都必须成功；实际成功数=")
                  + std::to_string(created));
        Check(!handles[kPoolCapacity].is_valid(), "window.handle.slot_exhausted_returns_invalid",
              "契约：64 槽用尽后 create_window 返回无效句柄");
        Check(!last_error_text().empty(), "window.handle.slot_exhausted_error_text",
              std::string("契约：槽满失败必须留下错误文本；实际=") + last_error_text());

        for (std::size_t index = 0; index < created && index < kPoolCapacity; ++index)
        {
            W::WindowPool::destroy_window(handles[index]);
        }

        const WindowHandle reused = W::WindowPool::create_window(make_desc("bench-reuse", false));
        Check(reused.is_valid(), "window.handle.slots_reusable_after_destroy",
              std::string("契约：销毁后槽位必须可再用（成功路径不清空既有错误文本，本条不回显陈旧文本）"));

        // 双 destroy：第二次指向空闲槽，契约要求"写日志、不改空闲链、不重复析构"
        W::WindowPool::destroy_window(reused);
        W::WindowPool::destroy_window(reused);
        const WindowHandle after_double = W::WindowPool::create_window(make_desc("bench-after-double", false));
        Check(after_double.is_valid(), "window.handle.double_destroy_keeps_free_list",
              std::string("契约：重复 destroy 不得破坏空闲链（随后的创建仍成功）；last_error=") + last_error_text());
        if (after_double.is_valid())
        {
            W::WindowPool::destroy_window(after_double);
        }

        // 越界句柄：公开面不提供裸值构造，用位拷贝造一个（可平凡复制，不触发 UB）
        const WindowHandle out_of_range = std::bit_cast<WindowHandle>(static_cast<W::WindowUINT>(200));
        W::WindowPool::destroy_window(out_of_range);
        const WindowHandle after_range = W::WindowPool::create_window(make_desc("bench-after-range", false));
        Check(after_range.is_valid(), "window.handle.out_of_range_destroy_keeps_free_list",
              std::string("契约：越界句柄（id=200 > 63）destroy_window 只写日志、不改空闲链、不重复析构；"
                          "进程存活且随后创建成功即为其证据；last_error=") + last_error_text());
        if (after_range.is_valid())
        {
            W::WindowPool::destroy_window(after_range);
        }

        // set_current_window 的无效入参：current 保持不变
        const WindowHandle current_a = W::WindowPool::create_window(make_desc("bench-current-a", false));
        const WindowHandle current_b = W::WindowPool::create_window(make_desc("bench-current-b", false));
        if (current_a.is_valid() && current_b.is_valid())
        {
            W::WindowPool::set_current_window(current_a);
            W::WindowPool::set_current_window(out_of_range);
            Check(&W::WindowPool::get_current_window() == &W::WindowPool::get_window(current_a),
                  "window.handle.set_current_rejects_invalid",
                  "契约：句柄无效/越界时 set_current_window 写日志、current 保持不变");
            W::WindowPool::set_current_window(current_b);
            Check(&W::WindowPool::get_current_window() == &W::WindowPool::get_window(current_b),
                  "window.handle.set_current_accepts_valid",
                  "契约：有效句柄 set_current_window 后 get_current_window 返回该窗口");
            W::WindowPool::destroy_window(current_a);
            W::WindowPool::destroy_window(current_b);
        }
        else
        {
            Check(false, "window.handle.set_current_rejects_invalid",
                  std::string("前置失败：当前窗口创建失败，last_error=") + last_error_text());
        }

        // 描述符违约：标题为空 / 尺寸为 0
        const WindowHandle bad_title = W::WindowPool::create_window(W::WindowDesc{ nullptr, W::WindowSize{ 320, 240 } });
        Check(!bad_title.is_valid() && error_has("create_window"),
              "window.handle.invalid_title_rejected",
              std::string("契约：title == nullptr 属描述符违约，返回无效句柄并写错误文本；last_error=") + last_error_text());

        W::WindowDesc zero_size{};
        zero_size.title      = "bench-zero-size";
        zero_size.is_visible = false;
        zero_size.size       = W::WindowSize{ 0, 0 };
        const WindowHandle bad_size = W::WindowPool::create_window(zero_size);
        Check(!bad_size.is_valid() && error_has("create_window"),
              "window.handle.zero_size_rejected",
              std::string("契约：尺寸为 0 属描述符违约，返回无效句柄并写错误文本；last_error=") + last_error_text());

        // 未初始化时的创建：返回无效句柄并写错误文本
        W::shutdown();
        const WindowHandle before_init = W::WindowPool::create_window(make_desc("bench-before-init", false));
        Check(!before_init.is_valid() && !last_error_text().empty(),
              "window.handle.create_before_init_rejected",
              std::string("契约：未初始化时 create_window 返回无效句柄并写错误文本；last_error=") + last_error_text());

        // 契约要求 std::abort() 的两个入口：同进程触发会终止整个测试进程，且本域无法新增子进程分裂参数
        Uncovered("window.handle.get_window_abort",
                  "契约：get_window(无效/越界/陈旧句柄) 写日志后 ID_DEBUG_BREAK() + std::abort()。同进程触发会终止"
                  "整个测试进程；子进程分裂入口在 Benchmark/src/main.cpp（不在本卡 writes，且框架禁止静态自注册），"
                  "本轮无隔离手段 → 按未覆盖登记，不伪装成 Skip/通过");
        Uncovered("window.handle.get_current_without_current_abort",
                  "契约：没有 current 时 get_current_window() 同样 abort；障碍同上（无子进程隔离）");
    }

    // ================= 用例 6：多窗隔离 =================

    void case_isolation() noexcept
    {
        if (!begin_case("window.isolation.init"))
        {
            return;
        }

        W::WindowDesc none_desc = make_desc("bench-iso-none", false);

        W::WindowDesc gl_desc = make_desc("bench-iso-gl", false);
        gl_desc.render_api              = W::RenderApi::OpenGL;
        gl_desc.render_desc.opengl.major = 3;
        gl_desc.render_desc.opengl.minor = 3;
        gl_desc.render_desc.opengl.profile = W::GLProfile::Core;

        W::WindowDesc vk_desc = make_desc("bench-iso-vk", false);
        vk_desc.render_api    = W::RenderApi::Vulkan;
        vk_desc.render_desc.vulkan = W::VulkanContextDesc{};

        const WindowHandle handle_none = W::WindowPool::create_window(none_desc);
        const WindowHandle handle_gl   = W::WindowPool::create_window(gl_desc);
        const WindowHandle handle_vk   = W::WindowPool::create_window(vk_desc);

        Check(handle_none.is_valid() && handle_gl.is_valid() && handle_vk.is_valid(),
              "window.isolation.mixed_render_api_create",
              std::string("None/OpenGL(3.3 Core)/Vulkan 三种描述符混合创建都必须成功（成功路径不清空既有错误文本，本条不回显陈旧文本）"));

        if (!handle_none.is_valid() || !handle_gl.is_valid() || !handle_vk.is_valid())
        {
            W::shutdown();
            return;
        }

        W::Window& window_none = W::WindowPool::get_window(handle_none);
        W::Window& window_gl   = W::WindowPool::get_window(handle_gl);
        W::Window& window_vk   = W::WindowPool::get_window(handle_vk);

        Check(&window_none == &W::WindowPool::get_window(handle_none)
                  && &window_gl == &W::WindowPool::get_window(handle_gl),
              "window.isolation.reference_address_stable",
              "契约：存活期间 Window& 地址稳定（固定槽 + placement new，不搬移）");

        {
            // 槽位搅动：再建一个再销毁，验证地址不变的结论不是「恰好没有搅动」
            const WindowHandle churn = W::WindowPool::create_window(make_desc("bench-iso-churn", false));
            if (churn.is_valid())
            {
                W::WindowPool::destroy_window(churn);
            }
        }
        Check(&window_none == &W::WindowPool::get_window(handle_none)
                  && &window_gl == &W::WindowPool::get_window(handle_gl)
                  && &window_vk == &W::WindowPool::get_window(handle_vk),
              "window.isolation.reference_address_stable_after_churn",
              "契约：其它窗口创建/销毁后，既有窗口引用地址仍不变");

        window_none.set_size(W::WindowSize{ 400, 300 });
        const W::WindowSize size_none = window_none.get_size();
        const W::WindowSize size_gl   = window_gl.get_size();
        const W::WindowSize size_vk   = window_vk.get_size();
        Check(size_none.width == 400 && size_none.height == 300, "window.isolation.size_change_applied",
              std::string("契约：set_size 后该窗口 get_size 返回新值；实际=")
                  + std::to_string(size_none.width) + "x" + std::to_string(size_none.height));
        Check(size_gl.width == 320 && size_gl.height == 240 && size_vk.width == 320 && size_vk.height == 240,
              "window.isolation.size_isolated",
              std::string("契约：尺寸按窗口独立；实际 gl=") + std::to_string(size_gl.width) + "x" + std::to_string(size_gl.height)
                  + " vk=" + std::to_string(size_vk.width) + "x" + std::to_string(size_vk.height));

        window_none.request_close();
        const W::WindowStatus status_none = window_none.get_status();
        const W::WindowStatus status_gl   = window_gl.get_status();
        const W::WindowStatus status_vk   = window_vk.get_status();
        Check(status_none.is_in_status(WindowStatusEnum::ShouldClose)
                  && !status_gl.is_in_status(WindowStatusEnum::ShouldClose)
                  && !status_vk.is_in_status(WindowStatusEnum::ShouldClose),
              "window.isolation.status_isolated",
              "契约：状态按窗口独立（request_close 只影响该窗口的关闭请求标志）");
        window_none.cancel_close();

        Check(window_none.get_handle() == handle_none && window_gl.get_handle() == handle_gl
                  && window_vk.get_handle() == handle_vk,
              "window.isolation.handle_identity",
              "契约：get_handle() 返回本窗口自己的句柄");

        // set_current_window 只影响 get_current_window，不参与事件路由（路由证据在事件用例）
        W::WindowPool::set_current_window(handle_none);
        Check(&W::WindowPool::get_current_window() == &window_none, "window.isolation.current_window_recorded",
              "契约：set_current_window 只记录应用层当前窗口");

        const W::WindowSize size_after_current = window_gl.get_size();
        Check(size_after_current.width == 320 && size_after_current.height == 240,
              "window.isolation.current_does_not_change_peers",
              "契约：set_current_window 不改变其它窗口的尺寸/状态（current 不参与任何查询转发）");

        W::shutdown();
    }

    // ================= 用例 7：事件按窗口投递 =================

#if defined(_WIN32)
    void send_message_to(void* hwnd, unsigned int message, std::uintptr_t wparam, std::intptr_t lparam) noexcept
    {
        if (hwnd == nullptr)
        {
            return;
        }
        ::SendMessageA(static_cast<HWND>(hwnd), message, static_cast<WPARAM>(wparam), static_cast<LPARAM>(lparam));
    }
#endif

    void case_events() noexcept
    {
        if (!begin_case("window.events.init"))
        {
            return;
        }

        const char* const titles[kEventSlots] = { "bench-ev-a", "bench-ev-b", "bench-ev-c" };

        for (int index = 0; index < kEventSlots; ++index)
        {
            g_slots[index].m_tally  = Tally{};
            g_slots[index].m_handle = W::WindowPool::create_window(make_desc(titles[index], false));
            if (!g_slots[index].m_handle.is_valid())
            {
                Check(false, "window.events.create_three_windows",
                      std::string("前置失败：创建窗口 ") + titles[index] + " 失败，last_error=" + last_error_text());
                W::shutdown();
                return;
            }
            register_all(W::WindowPool::get_window(g_slots[index].m_handle), g_slots[index]);
        }

        Check(true, "window.events.create_three_windows",
              "三个隐藏 RenderApi::None 窗口创建成功，每个窗口注册了全部 16 个回调入口");

        // current 指向 A：证明事件不按 current 转发
        W::WindowPool::set_current_window(g_slots[0].m_handle);

        W::Window& window_b = W::WindowPool::get_window(g_slots[1].m_handle);
        W::Window& window_c = W::WindowPool::get_window(g_slots[2].m_handle);

        // ---- 触发手段 1（公开入口）：set_size → 平台尺寸消息 → 该窗口自己的回调栈 ----
        window_b.set_size(W::WindowSize{ 400, 300 });
        W::poll_events();
        W::poll_events();

        const bool trigger_available = g_slots[1].m_tally.m_total > 0;
        Check(trigger_available, "window.events.trigger_available",
              std::string("触发手段：Window::set_size（隐藏窗口）+ poll_events；窗口 B 收到事件数=")
                  + std::to_string(g_slots[1].m_tally.m_total));

        if (trigger_available)
        {
            Check(g_slots[0].m_tally.m_total == 0 && g_slots[2].m_tally.m_total == 0,
                  "window.events.per_window_only",
                  std::string("契约：事件只到达产生它的窗口；A=") + std::to_string(g_slots[0].m_tally.m_total)
                      + " C=" + std::to_string(g_slots[2].m_tally.m_total) + "（都应保持 0）");
            Check(g_slots[1].m_tally.m_by_type[2] > 0, "window.events.resize_type_delivered",
                  std::string("契约：set_size 产生 WindowResize（type=2）；实际计数=")
                      + std::to_string(g_slots[1].m_tally.m_by_type[2]));
        }

        // ---- 后台窗口（非 current）同样收到自己的事件 ----
        window_c.set_size(W::WindowSize{ 360, 280 });
        W::poll_events();
        W::poll_events();
        const bool background_ok = !trigger_available
                                || (g_slots[2].m_tally.m_total > 0 && g_slots[0].m_tally.m_total == 0
                                    && g_slots[1].m_tally.m_by_type[2] > 0);
        Check(background_ok, "window.events.background_window_delivered",
              std::string("契约：非 current 的背景窗口无需先 set_current_window 即可收到自己的事件；C=")
                  + std::to_string(g_slots[2].m_tally.m_total) + " current=A");

        // ---- 注销生效 ----
        const std::uint32_t before_unregister = g_slots[2].m_tally.m_total;
        window_c.set_window_size_callback(nullptr, nullptr);
        window_c.set_framebuffer_size_callback(nullptr, nullptr);
        window_c.set_size(W::WindowSize{ 380, 300 });
        W::poll_events();
        W::poll_events();
        Check(g_slots[2].m_tally.m_total == before_unregister, "window.events.unregister_effective",
              std::string("契约：set_*_callback(nullptr) 即注销该组（fn == nullptr 表示注销）；注销后新增事件数=")
                  + std::to_string(g_slots[2].m_tally.m_total - before_unregister));
        register_all(window_c, g_slots[2]);

        // ---- 全局闸门 ----
        Check(W::Event::EventManager::is_initialized(), "window.events.manager_initialized",
              "契约：init 之后 EventManager::is_initialized() 为 true");
        Check(W::Event::EventManager::is_enabled(), "window.events.manager_enabled_by_default",
              "契约：EventManager::init 把事件闸门置为打开");

        const std::uint32_t before_gate = g_slots[2].m_tally.m_total;
        W::Event::EventManager::disable();
        const bool gate_state_off = !W::Event::EventManager::is_enabled();
        window_c.set_size(W::WindowSize{ 400, 320 });
        W::poll_events();
        W::poll_events();
        Check(gate_state_off && g_slots[2].m_tally.m_total == before_gate, "window.events.gate_disable_drops_all",
              std::string("契约：disable() 之后所有窗口的事件被静默丢弃（不缓存、不计数）；新增事件数=")
                  + std::to_string(g_slots[2].m_tally.m_total - before_gate));

        W::Event::EventManager::enable();
        window_c.set_size(W::WindowSize{ 420, 340 });
        W::poll_events();
        W::poll_events();
        Check(W::Event::EventManager::is_enabled() && g_slots[2].m_tally.m_total > before_gate,
              "window.events.gate_enable_restores",
              std::string("契约：enable() 之后事件恢复派发；当前位置计数=") + std::to_string(g_slots[2].m_tally.m_total));

        // ---- 触发手段 2（注入尝试）：Win32 消息投递到目标窗口的 HWND ----
        // 只对"平台本来会产生、且插件有对应回调槽"的类型做注入；未观测到不判失败，按触发手段不可得记 Skip。
#if defined(_WIN32)
        const W::NativeWindowInfo native_c = window_c.get_native_window_info();
        if (native_c.backend == W::NativeBackend::Win32 && native_c.win32.hwnd != nullptr)
        {
            void* const hwnd = native_c.win32.hwnd;
            const std::uint32_t before_injection_total = g_slots[2].m_tally.m_total;
            const std::uint32_t before_injection_a     = g_slots[0].m_tally.m_total;
            const std::uint32_t before_injection_b     = g_slots[1].m_tally.m_total;

            send_message_to(hwnd, WM_MOVE, 0, static_cast<std::intptr_t>(MAKELPARAM(24, 24)));
            send_message_to(hwnd, WM_PAINT, 0, 0);
            send_message_to(hwnd, WM_SETFOCUS, 0, 0);
            send_message_to(hwnd, WM_KILLFOCUS, 0, 0);

            const std::intptr_t key_lparam = static_cast<std::intptr_t>(MAKELPARAM(1, 1));
            send_message_to(hwnd, WM_KEYDOWN, VK_ESCAPE, key_lparam);
            send_message_to(hwnd, WM_KEYDOWN, VK_ESCAPE, key_lparam);
            send_message_to(hwnd, WM_KEYUP, VK_ESCAPE, static_cast<std::intptr_t>(key_lparam | 0xC0000000LL));

            W::poll_events();
            W::poll_events();

            const std::uint32_t injected = g_slots[2].m_tally.m_total - before_injection_total;
            Check(g_slots[0].m_tally.m_total == before_injection_a
                      && g_slots[1].m_tally.m_total == before_injection_b,
                  "window.events.injected_messages_only_to_target",
                  std::string("注入 Win32 消息（WM_MOVE/WM_PAINT/WM_SETFOCUS/WM_KILLFOCUS/WM_KEYDOWN×2/WM_KEYUP）"
                              "到目标窗口 HWND：其它两个窗口的计数必须完全不变；目标窗口新增事件数=")
                      + std::to_string(injected) + "（0 表示注入未产生可观测事件，见随后的逐类 Skip）");
        }
        else
        {
            Skip("window.events.injected_messages_only_to_target",
                 "本机后端不是 Win32 或拿不到原生 HWND：消息注入路径不可得");
        }
#else
        Skip("window.events.injected_messages_only_to_target", "非 Windows 平台：无 Win32 消息注入路径");
#endif

        // ---- 观测到的事件类型与未覆盖类型 ----
        bool covered[kEventTypeCount] = {};
        std::uint32_t covered_count = 0;
        for (std::uint32_t type = 1; type < kEventTypeCount; ++type)
        {
            std::uint32_t received = 0;
            for (int index = 0; index < kEventSlots; ++index)
            {
                received += g_slots[index].m_tally.m_by_type[type];
            }
            if (received > 0)
            {
                covered[type] = true;
                ++covered_count;
            }
        }

        std::string covered_names;
        for (std::uint32_t type = 1; type < kEventTypeCount; ++type)
        {
            if (!covered[type])
            {
                continue;
            }
            if (!covered_names.empty())
            {
                covered_names += ", ";
            }
            covered_names += kEventTypeNames[type];
            covered_names += "=";
            std::uint32_t received = 0;
            for (int index = 0; index < kEventSlots; ++index)
            {
                received += g_slots[index].m_tally.m_by_type[type];
            }
            covered_names += std::to_string(received);
        }
        ReportLine("    events observed        : " + std::to_string(covered_count) + "/23 类（不含 None）  "
                   + covered_names);

        // 逐类登记未观测到的类型：写明该类的触发手段为什么不可得
        for (const TriggerCategory& category : kUntriggerableCategories)
        {
            std::string missing;
            for (int index = 0; index < category.m_count; ++index)
            {
                const std::uint32_t type = category.m_types[index];
                if (covered[type])
                {
                    continue;
                }
                if (!missing.empty())
                {
                    missing += ", ";
                }
                missing += kEventTypeNames[type];
            }
            if (missing.empty())
            {
                continue;
            }
            Skip(std::string("window.events.type.") + kEventTypeNames[category.m_types[0]], missing + "：" + category.m_reason);
        }

        for (int index = 0; index < kEventSlots; ++index)
        {
            W::WindowPool::destroy_window(g_slots[index].m_handle);
            g_slots[index].m_handle = WindowHandle{};
        }
        W::shutdown();
    }

    // ================= 用例 8：状态位逐位 =================

    void case_status_bits() noexcept
    {
        // 位值契约（枚举值与报告下标一致，不重排）
        const bool bits_ok = static_cast<std::uint8_t>(WindowStatusEnum::Visible)     == (1u << 0)
                          && static_cast<std::uint8_t>(WindowStatusEnum::Focused)     == (1u << 1)
                          && static_cast<std::uint8_t>(WindowStatusEnum::Iconified)   == (1u << 2)
                          && static_cast<std::uint8_t>(WindowStatusEnum::Maximized)   == (1u << 3)
                          && static_cast<std::uint8_t>(WindowStatusEnum::Resizable)   == (1u << 4)
                          && static_cast<std::uint8_t>(WindowStatusEnum::Decorated)   == (1u << 5)
                          && static_cast<std::uint8_t>(WindowStatusEnum::ShouldClose) == (1u << 6);
        Check(bits_ok, "window.status.enum_bit_values",
              "契约：七个状态位分别是 1u<<0 … 1u<<6，None = 0，位宽单字节（尚余 1 位未使用）");

        const W::WindowStatus empty{};
        Check(empty.is_none(), "window.status.none_is_empty", "契约：默认构造得到空状态集合（is_none() 为 true）");

        const W::WindowStatus combined = WindowStatusEnum::Visible | WindowStatusEnum::Focused;
        Check(combined.is_in_status(WindowStatusEnum::Visible)
                  && combined.is_in_status(WindowStatusEnum::Focused)
                  && !combined.is_in_status(WindowStatusEnum::Iconified) && !combined.is_none(),
              "window.status.combine_and_query",
              "契约：operator| 返回位集合包装类，is_in_status 按位判定（未置位返回 false）");

        Check(sizeof(W::WindowStatus) == 1 && sizeof(W::WindowStatusEnum) == 1,
              "window.status.single_byte_layout",
              "契约：包装类只存一个位集合字，与枚举同宽（编译期断言同时钉住）");

        if (!begin_case("window.status.init"))
        {
            return;
        }

        // 隐藏 + 不可缩放 + 无装饰
        W::WindowDesc plain_desc = make_desc("bench-status-plain", false);
        plain_desc.is_resizable  = false;
        plain_desc.is_decorated  = false;

        // 隐藏 + 可缩放 + 有装饰
        W::WindowDesc decorated_desc = make_desc("bench-status-decorated", false);

        const WindowHandle plain_handle     = W::WindowPool::create_window(plain_desc);
        const WindowHandle decorated_handle = W::WindowPool::create_window(decorated_desc);

        Check(plain_handle.is_valid() && decorated_handle.is_valid(), "window.status.create_two_windows",
              std::string("前置：两个隐藏窗口创建成功（成功路径不清空既有错误文本，本条不回显陈旧文本）"));

        if (!plain_handle.is_valid() || !decorated_handle.is_valid())
        {
            W::shutdown();
            return;
        }

        W::Window& plain     = W::WindowPool::get_window(plain_handle);
        W::Window& decorated = W::WindowPool::get_window(decorated_handle);

        const W::WindowStatus plain_status = plain.get_status();
        Check(!plain_status.is_in_status(WindowStatusEnum::Visible), "window.status.visible_unset_when_hidden",
              "契约：is_visible = false 创建的窗口 Visible 位未置位");
        Check(!plain_status.is_in_status(WindowStatusEnum::Resizable)
                  && decorated.get_status().is_in_status(WindowStatusEnum::Resizable),
              "window.status.resizable_bit_follows_desc",
              "契约：Resizable 位与描述符 is_resizable 一致（false→未置位，true→置位）");
        Check(!plain_status.is_in_status(WindowStatusEnum::Decorated)
                  && decorated.get_status().is_in_status(WindowStatusEnum::Decorated),
              "window.status.decorated_bit_follows_desc",
              "契约：Decorated 位与描述符 is_decorated 一致");
        Check(!plain_status.is_in_status(WindowStatusEnum::Iconified)
                  && !plain_status.is_in_status(WindowStatusEnum::Maximized)
                  && !plain_status.is_in_status(WindowStatusEnum::ShouldClose),
              "window.status.iconified_maximized_shouldclose_unset",
              "契约：新建隐藏窗口的 Iconified/Maximized/ShouldClose 位均未置位");

        // 位与对应查询接口一致（同一份平台属性，不允许两套口径）
        Check(plain_status.is_in_status(WindowStatusEnum::Visible) == plain.is_visible()
                  && plain_status.is_in_status(WindowStatusEnum::Focused) == plain.is_focused()
                  && plain_status.is_in_status(WindowStatusEnum::ShouldClose) == plain.should_close(),
              "window.status.bits_match_queries",
              "契约：三个与公开查询接口重叠的位必须与对应查询结果一致（is_visible/is_focused/should_close）");

        Check(!plain.is_focused() && !plain_status.is_in_status(WindowStatusEnum::Focused),
              "window.status.focused_false_for_hidden_window",
              "契约：隐藏窗口不在前台，Focused 位与 is_focused() 均为 false");

        // request_close / cancel_close
        plain.request_close();
        Check(plain.should_close() && plain.get_status().is_in_status(WindowStatusEnum::ShouldClose),
              "window.status.request_close_sets_bit",
              "契约：request_close() 置位关闭请求标志（不销毁窗口、不派发）");
        plain.cancel_close();
        Check(!plain.should_close() && !plain.get_status().is_in_status(WindowStatusEnum::ShouldClose),
              "window.status.cancel_close_clears_bit",
              "契约：cancel_close() 复位关闭请求标志");

        // Visible 置位：唯一需要真正显示的取证（任务卡 §2 的隐藏约束在此子项例外，时长为实测值）
        const double visible_begin = NowNs();
        plain.show();
        const bool visible_set  = plain.get_status().is_in_status(WindowStatusEnum::Visible);
        const bool visible_query = plain.is_visible();
        plain.hide();
        const double visible_ms = (NowNs() - visible_begin) / 1.0e6;
        Check(visible_set && visible_query && !plain.is_visible()
                  && !plain.get_status().is_in_status(WindowStatusEnum::Visible),
              "window.status.visible_bit_set_by_show",
              std::string("契约：show() 后 Visible 位置位、hide() 后复位；show→hide 间隔=")
                  + FormatDouble(visible_ms, 3) + " ms（该子项是全程隐藏约束的唯一例外，随后立即隐藏）");

        Skip("window.status.iconified_bit_set",
             "置位需要窗口真实最小化；契约的窗口操作只有 show/hide/focus/lose_focus，没有 iconify 入口，"
             "隐藏窗口上注入 WM_SIZE(SIZE_MINIMIZED) 不构成「状态位跟随平台属性」的可靠证据 → 本轮不取证");
        Skip("window.status.maximized_bit_set",
             "置位需要窗口真实最大化；契约没有 maximize 操作入口，隐藏窗口上无法程序化进入最大化状态");

        W::shutdown();
    }

    // ================= 用例 9：GL 面 =================

    void case_gl() noexcept
    {
        if (!begin_case("window.gl.init"))
        {
            return;
        }

        W::WindowDesc desc = make_desc("bench-gl", false);
        desc.render_api                = W::RenderApi::OpenGL;
        desc.render_desc.opengl.major  = 3;
        desc.render_desc.opengl.minor  = 3;
        desc.render_desc.opengl.profile = W::GLProfile::Core;
        desc.render_desc.opengl.is_debug = true;      // 软偏好：请求值与实际值不同不构成失败

        const WindowHandle handle = W::WindowPool::create_window(desc);
        Check(handle.is_valid(), "window.gl.create_context_window",
              std::string("契约：OpenGL 3.3 Core 隐藏窗口创建成功（成功路径不清空既有错误文本，本条不回显陈旧文本）"));

        if (!handle.is_valid())
        {
            W::shutdown();
            return;
        }

        W::Window& window = W::WindowPool::get_window(handle);

        window.make_context_current();
        const W::ContextInfo info = window.get_context_info();
        Check(info.major >= 3 && info.minor >= 3, "window.gl.context_version_at_least_requested",
              std::string("契约：get_context_info 返回实际查询值（≥ 请求的 3.3）；实际=")
                  + std::to_string(info.major) + "." + std::to_string(info.minor));
        Check(info.profile == W::GLProfile::Core, "window.gl.context_profile_core",
              "契约：请求 Core profile 的上下文实际查询值也是 Core（硬约束）");
        ReportLine(std::string("    gl.is_debug            : requested=true actual=")
                   + (info.is_debug ? "true" : "false")
                   + "（软偏好，只记录不判失败；README §10.1：is_debug 只能在 GL 4.3+ 查询，更低版本恒 false）");

        const W::ProcAddress get_string_proc = window.get_proc_address("glGetString");
        Check(get_string_proc != nullptr, "window.gl.proc_address_nonnull",
              "契约：正确 current 上下文上 get_proc_address(\"glGetString\") 返回非空");
        Check(window.get_proc_address(nullptr) == nullptr, "window.gl.proc_address_null_name_null",
              "契约：入口名为空时返回 nullptr（不算失败、不写日志）");
        Check(window.get_proc_address("glDefinitelyNotAnEntryPoint") == nullptr,
              "window.gl.proc_address_unknown_null",
              "契约：查不到某入口是扩展探测的常规结果，返回 nullptr 且不写日志");

        // set_vsync 两档
        const std::string error_before_vsync(W::last_error());
        window.set_vsync(true);
        window.set_vsync(false);
        Check(error_before_vsync == W::last_error(), "window.gl.set_vsync_two_settings_no_violation",
              "契约：两档 set_vsync 都不写错误文本（前提：本线程 current 是该窗口的上下文）");

        const W::ProcAddress interval_proc = window.get_proc_address("wglGetSwapIntervalEXT");
        if (interval_proc != nullptr)
        {
            // wglGetSwapIntervalEXT 的签名是 int(*)()；x64 上 Windows 只有一种调用约定
            using SwapIntervalFn = int(*)();
            const SwapIntervalFn get_interval = reinterpret_cast<SwapIntervalFn>(interval_proc);
            window.set_vsync(true);
            const int interval_on = get_interval();
            window.set_vsync(false);
            const int interval_off = get_interval();
            Check(interval_on != 0 && interval_off == 0, "window.gl.set_vsync_effective",
                  std::string("契约：set_vsync 作用于本线程 current 的该窗口上下文；读回间隔 on=")
                      + std::to_string(interval_on) + " off=" + std::to_string(interval_off));
        }
        else
        {
            Skip("window.gl.set_vsync_effective",
                 "驱动未导出 wglGetSwapIntervalEXT：实际交换间隔不可读；契约亦声明 set_vsync 的驱动失败不可检出");
        }

        // 共享上下文：A 里创建 GL 对象，切到 B 的上下文后应可见（用 proc address 调 GL，不含 GL 头）
        window.release_context();
        W::WindowDesc shared_desc = desc;
        shared_desc.title = "bench-gl-shared";
        shared_desc.render_desc.opengl.share_with = handle;
        const WindowHandle shared_handle = W::WindowPool::create_window(shared_desc);
        Check(shared_handle.is_valid(), "window.gl.shared_context_created",
              std::string("契约：共享源存活、有 GL 上下文且未 current 时创建成功（成功路径不清空既有错误文本，本条不回显陈旧文本）"));

        if (shared_handle.is_valid())
        {
            W::Window& shared = W::WindowPool::get_window(shared_handle);

            window.make_context_current();
            const W::ProcAddress gen_proc   = window.get_proc_address("glGenBuffers");
            const W::ProcAddress del_proc   = window.get_proc_address("glDeleteBuffers");
            const W::ProcAddress is_proc    = window.get_proc_address("glIsBuffer");
            const W::ProcAddress bind_proc  = window.get_proc_address("glBindBuffer");

            if (gen_proc != nullptr && del_proc != nullptr && is_proc != nullptr && bind_proc != nullptr)
            {
                constexpr unsigned int kGlArrayBuffer = 0x8892u;     // GL_ARRAY_BUFFER

                using GenBuffersFn    = void(*)(int, unsigned int*);
                using DeleteBuffersFn = void(*)(int, const unsigned int*);
                using IsBufferFn      = unsigned char(*)(unsigned int);
                using BindBufferFn    = void(*)(unsigned int, unsigned int);

                // glGenBuffers 只**保留**名字，buffer 对象在第一次 glBindBuffer 时才真正创建；
                // 因此必须绑定后再用 glIsBuffer 判定，否则连未共享的上下文也会返回 GL_FALSE
                unsigned int buffer = 0;
                reinterpret_cast<GenBuffersFn>(gen_proc)(1, &buffer);
                reinterpret_cast<BindBufferFn>(bind_proc)(kGlArrayBuffer, buffer);

                shared.make_context_current();
                const unsigned char visible_in_shared = reinterpret_cast<IsBufferFn>(is_proc)(buffer);
                Check(buffer != 0 && visible_in_shared != 0, "window.gl.shared_context_shares_objects",
                      std::string("契约：共享上下文之间 GL 对象共享；A 绑定过的 buffer 在 B 的上下文里 glIsBuffer=")
                          + std::to_string(static_cast<unsigned>(visible_in_shared)) + " name=" + std::to_string(buffer));

                window.make_context_current();
                reinterpret_cast<BindBufferFn>(bind_proc)(kGlArrayBuffer, 0);
                reinterpret_cast<DeleteBuffersFn>(del_proc)(1, &buffer);
            }
            else
            {
                Skip("window.gl.shared_context_shares_objects",
                     "本机上下文拿不到 glGenBuffers/glBindBuffer/glIsBuffer/glDeleteBuffers 入口，"
                     "无法在不含 GL 头的前提下验证对象共享");
            }

            W::WindowPool::destroy_window(shared_handle);
        }

        // 跨线程交接：主线程 → release → 工作线程 make current → 工作线程取入口 → release → 主线程收回
        window.make_context_current();
        window.release_context();

        std::atomic<bool> handover_ok{ false };
        std::atomic<bool> handover_reached{ false };
        std::thread worker([&window, &handover_ok, &handover_reached]() noexcept {
            window.make_context_current();
            handover_ok      = (window.get_proc_address("glGetString") != nullptr);
            handover_reached = true;
            window.release_context();
        });
        worker.join();
        Check(handover_reached && handover_ok, "window.gl.cross_thread_handover",
              "契约：交接序列 release_context → 应用同步 → 新线程 make_context_current 必须成功，"
              "且新线程在该上下文上取到非空入口");

        window.make_context_current();

        // 同一上下文被两个线程同时绑定：另一线程的绑定必须被拒绝，主线程绑定保持
        std::atomic<bool> double_bind_rejected{ false };
        std::thread binder([&window, &double_bind_rejected]() noexcept {
            // 错误文本按线程各自一份：本线程首次调用前为空串，被拒绝后必须变化
            const std::string before = last_error_text();
            window.make_context_current();
            double_bind_rejected = (last_error_text() != before);
        });
        binder.join();
        Check(double_bind_rejected, "window.gl.double_bind_rejected",
              "契约：上下文已被另一线程持有时写日志与错误文本、不调用驱动、不改绑定");
        Check(window.get_proc_address("glGetString") != nullptr, "window.gl.binding_kept_after_rejection",
              "契约：被拒绝的绑定不得改变既有绑定（主线程仍能在该上下文上取到入口）");

        // 负路径：无 current / 非 GL 窗口。
        // 契约不规定错误文本措辞（README §9：诊断只有日志与 last_error 两条通道），因此判定用
        // "文本相对调用前发生变化" + 可观察的返回值，而不是猜具体措辞或函数名。
        window.release_context();
        const std::string before_no_context = last_error_text();
        const W::ContextInfo no_context = window.get_context_info();
        Check(no_context.major == 0 && no_context.minor == 0, "window.gl.context_info_zero_without_current",
              "契约：本线程没有 current 时 get_context_info 返回零值并写日志");
        Check(last_error_text() != before_no_context, "window.gl.context_info_zero_writes_error",
              std::string("契约：同一情形必须写错误文本（失败诊断走日志与 last_error）；实际=") + last_error_text());

        W::WindowDesc none_desc = make_desc("bench-gl-none", false);
        const WindowHandle none_handle = W::WindowPool::create_window(none_desc);
        if (none_handle.is_valid())
        {
            W::Window& none_window = W::WindowPool::get_window(none_handle);

            const std::string before_non_gl = last_error_text();
            none_window.make_context_current();
            const bool non_gl_error = last_error_text() != before_non_gl;
            const bool non_gl_no_proc = none_window.get_proc_address("glGetString") == nullptr;
            Check(non_gl_error && non_gl_no_proc, "window.gl.non_gl_window_rejected",
                  std::string("契约：非 GL 窗口上调用 GL 接口写日志、不调用平台（get_proc_address 也返回 nullptr）；last_error=")
                      + last_error_text());

            const std::string before_vsync = last_error_text();
            none_window.set_title(nullptr);                     // 先置成另一种违约文本（sentinel）
            const std::string vsync_sentinel = last_error_text();
            none_window.set_vsync(true);
            const bool vsync_error = last_error_text() != vsync_sentinel;
            none_window.set_title(nullptr);
            const std::string swap_sentinel = last_error_text();
            none_window.swap_buffers();
            const bool swap_error = last_error_text() != swap_sentinel;
            (void)before_vsync;
            Check(vsync_error && swap_error, "window.gl.non_gl_vsync_swap_rejected",
                  std::string("契约：非 GL 窗口上 set_vsync/swap_buffers 同样写日志且不调用平台；last_error=")
                      + last_error_text());
            W::WindowPool::destroy_window(none_handle);
        }

        W::WindowPool::destroy_window(handle);
        W::shutdown();
    }

    // ================= 用例 10：Vulkan Win32 WSI =================

    void case_vulkan() noexcept
    {
        if (!begin_case("window.vulkan.init"))
        {
            return;
        }

        W::WindowDesc desc = make_desc("bench-vk", false);
        desc.render_api    = W::RenderApi::Vulkan;
        desc.render_desc.vulkan = W::VulkanContextDesc{};

        const WindowHandle handle = W::WindowPool::create_window(desc);
        Check(handle.is_valid(), "window.vulkan.create_vulkan_window",
              std::string("契约：RenderApi::Vulkan 隐藏窗口创建成功（插件不建任何 Vulkan 对象）（成功路径不清空既有错误文本，本条不回显陈旧文本）"));

        if (handle.is_valid())
        {
            W::Window& window = W::WindowPool::get_window(handle);

            const W::VulkanWsiRequirements requirements = window.get_vulkan_wsi_requirements();
            Check(requirements.backend == W::NativeBackend::Win32, "window.vulkan.wsi_backend_win32",
                  "契约：Win32 后端返回 backend == NativeBackend::Win32");
            Check(requirements.extension_names != nullptr && requirements.extension_count == 2,
                  "window.vulkan.wsi_extension_count",
                  std::string("契约：Win32 后端返回两条实例扩展；实际条数=")
                      + std::to_string(requirements.extension_count));

            bool has_surface = false;
            bool has_win32   = false;
            for (std::uint32_t index = 0; index < requirements.extension_count
                                          && requirements.extension_names != nullptr; ++index)
            {
                const char* const name = requirements.extension_names[index];
                if (name == nullptr)
                {
                    continue;
                }
                if (std::strcmp(name, "VK_KHR_surface") == 0)
                {
                    has_surface = true;
                }
                if (std::strcmp(name, "VK_KHR_win32_surface") == 0)
                {
                    has_win32 = true;
                }
            }
            Check(has_surface && has_win32, "window.vulkan.wsi_extension_names",
                  "契约：扩展名表恰为 VK_KHR_surface 与 VK_KHR_win32_surface（需求声明，不是保证）");

            const W::NativeWindowInfo native = window.get_native_window_info();
            Check(native.backend == W::NativeBackend::Win32, "window.vulkan.native_backend_win32",
                  "契约：get_native_window_info 在 Win32 后端返回 NativeBackend::Win32");
            Check(native.win32.hwnd != nullptr && native.win32.hinstance != nullptr,
                  "window.vulkan.native_handles_nonnull",
                  "契约：Win32 分支借出 hwnd 与 hinstance（借用，不转移所有权）");
#if defined(_WIN32)
            Check(native.win32.hwnd != nullptr && ::IsWindow(static_cast<HWND>(native.win32.hwnd)) != FALSE,
                  "window.vulkan.native_hwnd_is_live_window",
                  "借出的 hwnd 必须是一个仍然存活的窗口（IsWindow 判定），否则 renderer 无法建 surface");
#endif

            W::WindowPool::destroy_window(handle);
        }

        // None 窗口的 WSI 查询：零值
        const WindowHandle none_handle = W::WindowPool::create_window(make_desc("bench-vk-none", false));
        if (none_handle.is_valid())
        {
            const W::VulkanWsiRequirements requirements =
                W::WindowPool::get_window(none_handle).get_vulkan_wsi_requirements();
            Check(requirements.backend == W::NativeBackend::None && requirements.extension_names == nullptr
                      && requirements.extension_count == 0,
                  "window.vulkan.non_vulkan_window_returns_zero",
                  "契约：非 Vulkan 窗口返回 backend=None、extension_names=nullptr、count=0（并写日志）");
            W::WindowPool::destroy_window(none_handle);
        }

        // loader / SDK 探测：缺失项写进 Skip 原因
#if defined(_WIN32)
        const HMODULE loader = ::LoadLibraryA("vulkan-1.dll");
        if (loader == nullptr)
        {
            Skip("window.vulkan.surface_and_instance",
                 "本机无 Vulkan loader（vulkan-1.dll 不可加载，GetLastError="
                     + std::to_string(static_cast<unsigned long>(::GetLastError()))
                     + "）：vkCreateInstance/vkCreateWin32SurfaceKHR 链路无法取证；缺失项 = Vulkan loader + Vulkan SDK 头");
            ReportLine("    vulkan.loader          : 不可加载（vulkan-1.dll 缺失）");
        }
        else
        {
            ::FreeLibrary(loader);
            Skip("window.vulkan.surface_and_instance",
                 "本机有 Vulkan loader，但仓库无 Vulkan SDK 头、renderer 侧 surface/instance 链路由 renderer 负责"
                 "（本卡 non_goals 明确不实现 Vulkan renderer 侧代码）：本轮只能取 WSI 需求表与原生句柄两条正确性证据");
            ReportLine("    vulkan.loader          : vulkan-1.dll 可加载（未创建 instance/surface：无 SDK 头与 renderer 侧实现）");
        }
#endif

        W::shutdown();
    }

    // ================= 用例 11：违约冷路径（写日志、不改现态） =================

    void case_violations() noexcept
    {
        if (!begin_case("window.violations.init"))
        {
            return;
        }

        const WindowHandle handle = W::WindowPool::create_window(make_desc("bench-violation", false));
        if (!handle.is_valid())
        {
            Check(false, "window.violations.create_window", std::string("前置失败：") + last_error_text());
            W::shutdown();
            return;
        }

        W::Window& window = W::WindowPool::get_window(handle);
        const char* const title_before = window.get_title();

        // 错误文本通道的判定方式：契约不规定文本措辞，只规定"失败会写本线程错误文本"，也从不规定成功要清空。
        // 因此先用**另一种**违约把文本置成已知内容（sentinel），再执行被测调用；文本变化即证明被测调用写了错误。
        // 每种 sentinel 都刻意与目标调用不同，避免"两次同样的失败写同样文本"造成的假阴性。

        window.set_size(W::WindowSize{ 0, 0 });                     // sentinel：尺寸违约文本
        const std::string title_sentinel = last_error_text();
        window.set_title(nullptr);
        const bool title_error = last_error_text() != title_sentinel;
        Check(title_error && window.get_title() == title_before, "window.violations.null_title_rejected",
              std::string("契约：空标题写日志、不改现态；标题指针与驳回前相同；last_error=") + last_error_text());

        window.set_title(nullptr);                                  // sentinel：标题违约文本
        const std::string size_sentinel = last_error_text();
        window.set_size(W::WindowSize{ 0, 0 });
        const bool size_error = last_error_text() != size_sentinel;
        const W::WindowSize size_after = window.get_size();
        Check(size_error && size_after.width == 320 && size_after.height == 240,
              "window.violations.zero_size_rejected",
              std::string("契约：尺寸为 0 写日志、不改现态；实际尺寸=") + std::to_string(size_after.width)
                  + "x" + std::to_string(size_after.height));

        window.set_title(nullptr);                                  // sentinel：标题违约文本
        const std::string key_sentinel = last_error_text();
        const bool key_pressed = window.get_key_state(W::KeyCodes::Unknown);
        const bool key_error = last_error_text() != key_sentinel;
        Check(!key_pressed && key_error, "window.violations.invalid_key_rejected",
              std::string("契约：域外键码写日志并返回 false；last_error=") + last_error_text());

        window.set_title(nullptr);                                  // sentinel：标题违约文本
        const std::string mouse_sentinel = last_error_text();
        const bool mouse_pressed = window.get_mouse_button_state(W::KeyCodes::A);
        const bool mouse_error = last_error_text() != mouse_sentinel;
        Check(!mouse_pressed && mouse_error, "window.violations.keyboard_key_as_mouse_rejected",
              std::string("契约：鼠标键判定只接受鼠标域内的键码，违约写日志并返回 false；last_error=") + last_error_text());

        W::shutdown();
    }
} // namespace

namespace ID::Bench
{
    void IdWindowCorrectnessTest() noexcept
    {
        ReportLine("  config (IDWindow): windows=hidden (is_visible=false) size=320x240"
                   " | render_api=None/OpenGL3.3Core/Vulkan | slots=64 | trigger=Window::set_size + poll_events");
        ReportLine("  unverified (IDWindow): Linux/macOS、MSVC 与 clang 编译面、Vulkan 非 Win32 后端、"
                   "Vulkan surface/instance 链路（无 SDK 头与 renderer 侧实现）、人工输入类事件");

        ReportLine("  note (T25): 报告头 build 行的 LINK= 是由 CMake 按三个目标的实际类型派生的"
                   "（IDWindow / IDLog / IDJson 并列）；紧随其后的 `plugin:` 行前缀 'IDLog=LINK ' 是公共设施"
                   "里为单插件写的固定文案（Benchmark/src/support/benchmark_support.cpp 不在本卡 writes 内），"
                   "读形态时以 build 行为准。");
        ReportLine("  consumer (T25): 本可执行文件即最小消费者——正确性 TU 只包含公开头，不含 GLFW/平台/"
                   "Vulkan/IDLog 头；性能 TU 为接入成本对照额外包含 vendor GLFW 头（对照实现，不属于消费者契约）。"
                   "STATIC 与 SHARED 两形态各完成一次构建与运行，证据见 out/verify/T25/。");

        case_link_and_export();
        case_public_headers();
        case_access_control();
        case_lifecycle();
        case_handles();
        case_isolation();
        case_events();
        case_status_bits();
        case_gl();
        case_vulkan();
        case_violations();
    }
} // namespace ID::Bench
