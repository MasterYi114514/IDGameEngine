#pragma once

/**
 *  `id.project` 子域各用例组的入口声明（私有头，只被 `src/id/id_project_*.cpp` 与
 *  `src/id/id_project_correctness.cpp` 包含）。
 *
 *  分组与 spec §15 / `docs/id/11-verification-matrix.md` §11.4 的组一致：
 *  AB（正例 A1–A9 与 A1 的 G8 限制补充用例、幂等 B1–B5）、C（输入非法 C1–C26）、
 *  DE（无写 D1–D7、用户保护 E1–E7）、F（故障注入 F0–F15）、H（恢复 H1–H8）、I（构建 I1–I15）、
 *  JKLMN（路径安全/裁剪/记录/命名/并发）。
 *  每组自包含：自己建 `CaseRoot`、自己造夹具，不读别的用例留下的目录。
 */

namespace ID::Bench
{
    /// @brief 正例（A1–A9）与幂等（B1–B5）组
    void IdProjectCasesAB() noexcept;

    /// @brief 输入非法组（C1–C26）
    void IdProjectCasesC() noexcept;

    /// @brief 无写（D1–D6）与用户保护（E1–E7）组
    void IdProjectCasesDE() noexcept;

    /// @brief 故障注入组（F0–F15）
    void IdProjectCasesF() noexcept;

    /// @brief 恢复组（H1–H8）
    void IdProjectCasesH() noexcept;

    /// @brief 构建组（I1–I15）
    void IdProjectCasesI() noexcept;

    /// @brief 路径安全 / 裁剪 / 记录 / 命名 / 并发组（J、K、L、M、N）
    void IdProjectCasesJKLMN() noexcept;
} // namespace ID::Bench
