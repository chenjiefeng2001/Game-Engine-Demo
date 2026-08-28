@echo off
setlocal
rem ══════════════════════════════════════════════════════════════════
rem MSVC ASan interception 缺陷防护（docs/Avalonia-Phase3-Freeze-ASan-Report.md）
rem interception_win.cpp:193：ASan 构建的 exe 直接运行时，进程启动早期对
rem 已加载模块做 inline hook，遇到无法解码的指令序列时缺省 CHECK 硬失败 →
rem 进程挂死（无响应、无法退出，需强制结束 + rename 绕过 exe/pdb 文件锁）。
rem 引擎/测试 exe 没有 AvaloniaEditor Program.cs 式"首行自设"位置
rem （env 必须在 CRT/ASan 初始化前生效，main() 内设置已太晚），
rem 因此直接运行统一走本脚本前置设置 env。
rem
rem 用法: run_test.cmd <test-exe> [args...]
rem 例  : run_test.cmd build\tests\Debug\test_bridge.exe --gtest_filter=EditorBridgeTest.*
rem       run_test.cmd build\tests\Debug\test_bridge.exe
rem ══════════════════════════════════════════════════════════════════
if "%~1"=="" (
    echo usage: run_test.cmd ^<test-exe^> [args...]
    exit /b 1
)
set "ASAN_WIN_CONTINUE_ON_INTERCEPTION_FAILURE=1"
call %*
exit /b %ERRORLEVEL%
