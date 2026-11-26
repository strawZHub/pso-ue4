@echo off
rem Builds addon\xinput1_3.dll - reproducible (same source = same file, same hash).
rem Needs the exact compiler: pip install ziglang==0.17.1
cd /d "%~dp0addon"
python -m ziglang cc -target x86_64-windows-gnu pso_addon.c xinput1_3.def -lwinhttp -luser32 -lgdi32 -lgdiplus -lws2_32 -lwinmm -shared -O2 -s -o xinput1_3.dll
echo.
echo Built. Check it matches the published hash:
certutil -hashfile xinput1_3.dll SHA256
pause
