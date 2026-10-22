/**
 *  @file
 *  @brief 组装 fixture m01 的正向对照 TU：传递可见的身份宏 ≠ 组 matched 成员
 *
 *  产品：catalog = {FX.Core, FX.Via, FX.Consumer.Fp}，profile 选中三者。
 *  真实 PUBLIC 链：IDFxConsumerFp -> IDFxVia -> IDFxCore。
 *  因此 consumer 的 TU 会**同时**看到 ID_USE_PLUGIN_IDFXVIA 与 ID_USE_PLUGIN_IDFXCORE，
 *  但 consumer 第 0 组（候选只有 FX.Via）的 matched 成员只有 FX.Via。
 *
 *  本 TU 必须**编译通过**：它同时证明"陷阱真实存在"（FX.Core 的身份宏确实传递可见）与
 *  "consumer 有权威手段区分两者"（ID_MATCHED_MEMBER_*）。
 *  配套的负向 TU 是 FixtureMatchedSetForeignMember.cpp：若把 FX.Core 当成第 0 组成员，那条 TU 必须编译失败。
 *
 *  @note 由 cmake/RunFixtureMatrix.cmake 编译（-fsyntax-only），不链接、不运行、不测量。
 */

#include <cstdint>

#include "IDFxConsumerFpMatched.generated.hpp"

// ---- 1. 传递可见：链上两个上游的身份宏都必须出现，否则负向用例是空转 ----
#if !defined(ID_USE_PLUGIN_IDFXVIA)
#    error "control: ID_USE_PLUGIN_IDFXVIA must be visible (consumer's group 0 member)"
#endif
#if !defined(ID_USE_PLUGIN_IDFXCORE)
#    error "control: ID_USE_PLUGIN_IDFXCORE must be visible transitively through IDFxVia (that is the trap)"
#endif

// ---- 2. 组 matched 集合（权威来源是 ID_MATCHED_*）：第 0 组恰好命中 FX.Via ----
#if ID_MATCHED_GROUP_COUNT_IDFXCONSUMERFP != 1
#    error "control: consumer must declare exactly one requirement group"
#endif
#if ID_MATCHED_COUNT_IDFXCONSUMERFP_G0 != 1
#    error "control: group 0 must have exactly one matched member"
#endif
#if !defined(ID_MATCHED_MEMBER_IDFXCONSUMERFP_G0_IDFXVIA)
#    error "control: FX.Via must be listed as a matched member of group 0"
#endif

// ---- 3. 误判防线：传递可见的 FX.Core 不是第 0 组的匹配成员 ----
#if defined(ID_MATCHED_MEMBER_IDFXCONSUMERFP_G0_IDFXCORE)
#    error "control: FX.Core must not be listed as a matched member of group 0"
#endif
