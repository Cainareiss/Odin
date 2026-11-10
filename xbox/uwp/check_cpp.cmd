@echo off
rem Standalone compile check for a bridge C++ file:  check_cpp.cmd bridge\file.cpp   (objects go to %OBJDIR%, default tmp\obj)
setlocal
set ROOT=C:\claude-work\odinuwp\uwp
set TEMP=C:\claude-work\tmp
set TMP=C:\claude-work\tmp
if "%OBJDIR%"=="" set OBJDIR=C:\claude-work\tmp\obj
call "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat" x64 uwp 10.0.26100.0 >nul || exit /b 1
if not exist %OBJDIR% mkdir %OBJDIR%
cl /nologo /c /std:c++20 /EHsc /utf-8 /W3 /DWINAPI_FAMILY=WINAPI_FAMILY_APP /D_UWP /DSWANUI_XBOX=1 /DTSUGARU_UWP=1 /DUNICODE /D_UNICODE /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS /I%ROOT%\bridge /I%ROOT%\swanui /I%ROOT%\swanui\compat /I%ROOT%\swanui\src /I%ROOT%\swanui\third_party\stb /I%ROOT%\swanui\third_party\swanstation\dep\libretro-common\include /Fo:%OBJDIR%\ %ROOT%\%1
exit /b %errorlevel%
