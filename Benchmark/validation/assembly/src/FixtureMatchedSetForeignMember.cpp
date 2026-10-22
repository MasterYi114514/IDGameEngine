/**
 *  @file
 *  @brief 组装 fixture m01 的负向编译用例：把传递可见的身份宏当成组 matched 成员必须编译失败
 *
 *  场景与 FixtureMatchedSetControl.cpp 相同（catalog = {FX.Core, FX.Via, FX.Consumer.Fp}，
 *  profile 选中三者，consumer 第 0 组候选只有 FX.Via）。IDFxCore 的身份宏沿
 *  IDFxConsumerFp -> IDFxVia -> IDFxCore 传递可见，但它**不是**第 0 组的 matched 成员。
 *
 *  本 TU 模拟错误实现："看到 ID_USE_PLUGIN_IDFXCORE 就把 FX.Core 当成第 0 组成员"，
 *  于是断言 ID_MATCHED_MEMBER_IDFXCONSUMERFP_G0_IDFXCORE 存在。该宏不会被生成，
 *  因此本 TU **必须编译失败**，且失败信息里恰好 1 条 `EXPECTED:MatchedSetForeignMember` 标记。
 *
 *  @note 断言口径：先有同环境的合法对照（FixtureMatchedSetControl.cpp 编译通过），
 *        再让本 TU 按具体期望失败；不接受"只看退出码非 0"。
 *        由 cmake/RunFixtureMatrix.cmake 编译（-fsyntax-only），不链接、不运行。
 */

#include <cstdint>

#include "IDFxConsumerFpMatched.generated.hpp"

// 前置：FX.Core 的身份宏确实传递可见。若它不可见，本负向用例是空转，必须显式暴露。
#if !defined(ID_USE_PLUGIN_IDFXCORE)
#    error "negative case would be vacuous: ID_USE_PLUGIN_IDFXCORE is not visible at all"
#endif

// 错误实现：把传递可见的身份宏当成"本组 matched 成员"。生成头里没有这条 member 宏。
#if !defined(ID_MATCHED_MEMBER_IDFXCONSUMERFP_G0_IDFXCORE)
#    error "EXPECTED:MatchedSetForeignMember - FX.Core is only transitively visible; it is not a matched member of consumer group 0, so a consumer that treats ID_USE_PLUGIN_* as group membership must not compile"
#endif
