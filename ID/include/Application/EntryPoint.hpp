#pragma once

#include <Application/Application.hpp>
#include <IDMath.hpp>

#include <chrono>
#include <cstdio>
#include <sstream>
#include <string>

#include "Json.hpp"
#include "JsonParser.hpp"
#include "ArenaManager.hpp"

/*
*   build_json_string — 程序化生成一个大型嵌套 JSON 用于压力测试
*
*   生成结构：
*   {
*       "name": "IDJson Stress Test",
*       "version": 1,
*       "data": [ ... 大量嵌套对象 ... ]
*   }
*/
static std::string build_json_string(int object_count)
{
    std::ostringstream ss;
    ss << "{\"name\":\"IDJson Stress Test\",\"version\":1,\"data\":[";

    for(int i = 0; i < object_count; ++i)
    {
        if(i > 0) ss << ",";

        ss << "{"
           << "\"id\":" << i << ","
           << "\"label\":\"item_" << i << "\","
           << "\"active\":" << (i % 2 == 0 ? "true" : "false") << ","
           << "\"score\":" << (static_cast<double>(i) * 3.14159) << ","
           << "\"tags\":[\"tag_a\",\"tag_b\",\"tag_c\"],"
           << "\"nested\":{"
           << "\"x\":" << (i % 100) << ","
           << "\"y\":" << ((i * 7) % 100) << ","
           << "\"name\":\"nested_" << i << "\""
           << "},"
           << "\"values\":[" << (i % 10) << "," << ((i + 1) % 10) << "," << ((i + 2) % 10) << "]"
           << "}";
    }

    ss << "]}";
    return ss.str();
}

/*
*   run_stress_test — 对 IDJson 进行压力测试
*
*   1. 生成指定数量对象的 JSON 字符串
*   2. 创建 Arena，解析 JSON
*   3. 测量耗时，随机采样验证数据正确性
*   4. 销毁 Arena，输出报告
*/
static void run_stress_test(int object_count)
{
    using namespace ID;

    std::printf("\n");
    std::printf("══════════════════════════════════════════\n");
    std::printf("  IDJson 压力测试 — %d 个对象\n", object_count);
    std::printf("══════════════════════════════════════════\n");

    // ── 1. 生成 JSON 字符串 ──
    auto t0 = std::chrono::steady_clock::now();
    std::string json_str = build_json_string(object_count);
    auto t1 = std::chrono::steady_clock::now();

    double gen_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    std::printf("  JSON 生成  : %zu 字节, 耗时 %.2f ms\n", json_str.size(), gen_ms);

    // ── 2. 创建 Arena ──
    ArenaID arena = ArenaManager::create_arena(ArenaManager::DEFAULT_CAPACITY);

    // ── 3. 解析 JSON ──
    t0 = std::chrono::steady_clock::now();
    Json root = JSON::parse(json_str, arena);
    t1 = std::chrono::steady_clock::now();

    double parse_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    double throughput = (json_str.size() / (1024.0 * 1024.0)) / (parse_ms / 1000.0);
    std::printf("  解析耗时  : %.2f ms  ( %.2f MB/s )\n", parse_ms, throughput);

    // ── 4. 验证数据 ──
    t0 = std::chrono::steady_clock::now();

    int errors = 0;

    // 验证顶层字段
    if(!root.is_object()) { ++errors; std::printf("  [错误] root 应为 Object\n"); }
    if(root["name"].as_string() != "IDJson Stress Test") { ++errors; }
    if(root["version"].as_int() != 1) { ++errors; }

    // 验证 data 数组
    const Json& data = root["data"];
    if(!data.is_array()) { ++errors; std::printf("  [错误] data 应为 Array\n"); }
    if(static_cast<int>(data.size()) != object_count) { ++errors; }

    // 随机采样验证若干元素
    int samples[] = {0, object_count / 4, object_count / 2, object_count * 3 / 4, object_count - 1};
    for(int idx : samples)
    {
        if(idx < 0 || idx >= object_count) continue;

        const Json& item = data[idx];
        if(item["id"].as_int() != idx) { ++errors; }

        const Json& nested = item["nested"];
        if(nested["x"].as_int() != idx % 100) { ++errors; }
        if(nested["y"].as_int() != (idx * 7) % 100) { ++errors; }

        const Json& tags = item["tags"];
        if(tags.size() != 3) { ++errors; }

        const Json& values = item["values"];
        if(values.size() != 3) { ++errors; }
        if(values[0u].as_int() != idx % 10) { ++errors; }
    }

    t1 = std::chrono::steady_clock::now();
    double verify_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

    if(errors == 0)
        std::printf("  数据验证  : ✅ 全部通过 ( %.2f ms )\n", verify_ms);
    else
        std::printf("  数据验证  : ❌ %d 个错误 ( %.2f ms )\n", errors, verify_ms);

    // ── 5. 输出结构概览 ──
    std::printf("\n  结构概览:\n");
    std::printf("    root.type()        = Object\n");
    std::printf("    root.size()        = %zu\n", root.size());
    std::printf("    root[\"data\"].size() = %zu\n", data.size());
    std::printf("    data[0].type()     = Object\n");
    std::printf("    data[0].size()     = %zu\n", data[0u].size());

    // ── 6. 清理 ──
    ArenaManager::destroy_arena(arena);

    std::printf("\n  总耗时: 生成 %.2f ms + 解析 %.2f ms + 验证 %.2f ms = %.2f ms\n",
                gen_ms, parse_ms, verify_ms, gen_ms + parse_ms + verify_ms);
    std::printf("══════════════════════════════════════════\n\n");
}

int main()
{
    // ═══════════════════════════════════════════
    //  IDJson 压力测试
    // ═══════════════════════════════════════════

    // 小规模热身
    run_stress_test(100);

    // 中等规模
    run_stress_test(1000);

    // 大规模
    run_stress_test(10000);

    // ═══════════════════════════════════════════
    //  启动引擎（可选：注释掉下面两行可纯测 JSON）
    // ═══════════════════════════════════════════

    std::printf("启动游戏引擎...\n\n");

    ID::Application* app = ID::create_application();
    app->run();

    return 0;
}
