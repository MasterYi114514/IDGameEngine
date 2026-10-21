@echo off
rem MSVC 语法检查：只编译 Benchmark/src/kernel/kernel_performance.cpp 一个 TU（不链接、不运行）
rem 目的：确认 opaque_target 的 _MSC_VER 分支（volatile 局部对象）在 MSVC/x64 下能编译
call "D:\Software\VS2026\Insiders\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul
if errorlevel 1 exit /b 10
cl /nologo /std:c++20 /utf-8 /W4 /GR- /EHs-c- /c ^
   /I "F:\IDGameEngine\Benchmark\include" ^
   /I "F:\IDGameEngine\ID\include" ^
   /DID_BENCHMARK_REPORT_DIR="\"F:/IDGameEngine/Benchmark\"" ^
   /DID_BENCH_CONFIG="\"Release\"" /DID_BENCH_LTO=0 /DID_BENCH_AVX2=1 /DID_BENCH_MONOLITHIC=0 ^
   /Fo"%~dp0kernel_performance.msvc.obj" ^
   "F:\IDGameEngine\Benchmark\src\kernel\kernel_performance.cpp"
exit /b %errorlevel%
