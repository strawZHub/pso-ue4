@echo off
rem builds addon\xinput1_3.dll - needs zig (https://ziglang.org) on PATH
cd /d "%~dp0addon"
zig cc -target x86_64-windows-gnu pso_addon.c xinput1_3.def -lwinhttp -luser32 -lgdi32 -lgdiplus -lws2_32 -shared -O2 -o xinput1_3.dll
pause
