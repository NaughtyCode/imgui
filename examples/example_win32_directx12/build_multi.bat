@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat" > nul 2>&1
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%
set OUT_DIR=Debug
set OUT_EXE=example_win32_directx12
set INCLUDES=/I..\.. /I..\..\backends
set SOURCES=main.cpp ..\..\backends\imgui_impl_dx12.cpp ..\..\backends\imgui_impl_win32.cpp ..\..\imgui.cpp ..\..\imgui_demo.cpp ..\..\imgui_draw.cpp ..\..\imgui_tables.cpp ..\..\imgui_widgets.cpp
set LIBS=d3d12.lib d3dcompiler.lib dxgi.lib
mkdir Debug 2>nul
cl /nologo /Zi /MD /utf-8 %INCLUDES% /D UNICODE /D _UNICODE %SOURCES% /Fe%OUT_DIR%/%OUT_EXE%.exe /Fo%OUT_DIR%/ /link %LIBS%
