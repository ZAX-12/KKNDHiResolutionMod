@echo off
rem Builds the mod:
rem   KKND Resolution Launcher.exe  - C# compiler that ships with Windows (.NET Framework 4)
rem   KRESMOD.dll                   - Visual Studio C++ tools (x86), found with vswhere
setlocal
cd /d "%~dp0"

set CSC=%WINDIR%\Microsoft.NET\Framework64\v4.0.30319\csc.exe
if not exist "%CSC%" set CSC=%WINDIR%\Microsoft.NET\Framework\v4.0.30319\csc.exe
set ICON=
if exist "..\goggame-1207659107.ico" set ICON="/win32icon:..\goggame-1207659107.ico"
"%CSC%" /nologo /target:winexe /optimize+ %ICON% "/out:KKND Resolution Launcher.exe" /r:System.Windows.Forms.dll /r:System.Drawing.dll KKNDResLauncher.cs || exit /b 1

set VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VSDIR=%%i
if not defined VSDIR (
    echo Visual Studio C++ tools not found - KRESMOD.dll not built.
    exit /b 1
)
set "PATH=%PATH%;%ProgramFiles(x86)%\Microsoft Visual Studio\Installer"
call "%VSDIR%\VC\Auxiliary\Build\vcvars32.bat" >nul
cd /d "%~dp0"
if not exist obj mkdir obj
cl /nologo /O2 /W3 /MT /LD /D_CRT_SECURE_NO_WARNINGS /Foobj\ dll\kresmod.c /Fe:KRESMOD.dll /link /DEF:dll\kresmod.def /IMPLIB:obj\kresmod.lib user32.lib || exit /b 1
