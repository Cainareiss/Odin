@echo off
rem Regenerates the shader headers from plat_d3d11.hlsl with the Windows SDK's fxc (shader model 4.0, feature level 10_0+).
setlocal
set FXC="C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\fxc.exe"
cd /d "%~dp0"
%FXC% /nologo /T vs_4_0 /E VSMain   /Vn g_plat_vs      /Fh plat_d3d11_vs.h      plat_d3d11.hlsl || exit /b 1
%FXC% /nologo /T ps_4_0 /E PSMain   /Vn g_plat_ps      /Fh plat_d3d11_ps.h      plat_d3d11.hlsl || exit /b 1
%FXC% /nologo /T ps_4_0 /E PSGame   /Vn g_plat_ps_game /Fh plat_d3d11_game_ps.h plat_d3d11.hlsl || exit /b 1
%FXC% /nologo /T vs_4_0 /E VSFull   /Vn g_plat_vs_full /Fh plat_d3d11_full_vs.h plat_d3d11.hlsl || exit /b 1
%FXC% /nologo /T ps_4_0 /E PSHdrOut /Vn g_plat_ps_hdr  /Fh plat_d3d11_hdr_ps.h  plat_d3d11.hlsl || exit /b 1
exit /b 0
