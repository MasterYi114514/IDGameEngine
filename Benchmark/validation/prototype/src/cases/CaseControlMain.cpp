#include <cstdio>

/**
 *  反向用例的合法对照可执行文件
 *  28 个 case TU 的合法分支被编成 OBJECT 库并链接进本程序：既编译通过又链接成功，
 *  说明后续"期望失败"用例的失败来自输入非法，而不是工具链或环境损坏。
 */

int main()
{
    std::printf("fixture-case-controls: PASS\n");
    return 0;
}
