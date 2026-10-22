/**
 *  @file
 *  @brief 组装 fixture 的指纹探针：把生成头的编译期常量 kProductFingerprint 打印成一行十进制数
 *
 *  用途：等价重排（catalog 顺序 / profile 顺序 / 候选顺序）必须得到同一个指纹。
 *  CMake 侧**不重算**这个数值（重算等于维护第二份算法），因此只能取真实编译期结果：
 *  本 TU 只 include 生成头与 ID 头，不链接任何插件或内核库（kPlan 是 consteval 结果）。
 *
 *  @note 由 cmake/RunFixtureMatrix.cmake 逐个正例编译并运行；输出为 `<十进制指纹>` 单行。
 *        本探针不测量任何时间，也不产生评级。
 */

#include <cstdio>

#include "IDAssembly.generated.hpp"

int main() noexcept
{
    static_assert(ID::Assembly::Generated::kSelectedCount > 0,
                  "fingerprint probe expects a non-empty product");
    std::printf("%llu\n", static_cast<unsigned long long>(ID::Assembly::Generated::kProductFingerprint));
    return 0;
}
