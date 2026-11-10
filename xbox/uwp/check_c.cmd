@echo off
rem Standalone compile check for a frontend C file:  check_c.cmd swanui\src\library.c
setlocal
set ROOT=C:\claude-work\odinuwp\uwp
set TEMP=C:\claude-work\tmp
set TMP=C:\claude-work\tmp
call "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat" x64 uwp 10.0.26100.0 >nul || exit /b 1
if not exist C:\claude-work\tmp\objc mkdir C:\claude-work\tmp\objc
cl /nologo /c /TC /std:c17 /utf-8 /W3 /DSWANUI_XBOX=1 /DTSUGARU_UWP=1 /D_UWP /DWINAPI_FAMILY=WINAPI_FAMILY_APP /DWIN32 /DNDEBUG /D_CRT_SECURE_NO_WARNINGS /D_CRT_NONSTDC_NO_DEPRECATE /DUNICODE /D_UNICODE /FI%ROOT%\swanui\compat\swanui_compat.h /I%ROOT%\swanui\compat /I%ROOT%\swanui\src /I%ROOT%\swanui\third_party\stb /I%ROOT%\swanui\third_party\swanstation\dep\libretro-common\include /I%ROOT%\swanui\third_party\rcheevos\include /Fo:C:\claude-work\tmp\objc\ %ROOT%\%1
exit /b %errorlevel%
