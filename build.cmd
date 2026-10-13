@echo off
rem Builds the mod:
rem   KKNDHiResolutionMod.exe  - C# compiler that ships with Windows (.NET Framework 4)
rem   KKNDHiResolutionMod.dll  - Visual Studio C++ tools (x86), found with vswhere
rem
rem   build.cmd           local build (uses the game's icon if it is in the parent folder)
rem   build.cmd release   clean build without the game's icon, packaged as
rem                       release\KKNDHiResolutionMod-%VERSION%.zip (only the exe and the DLL)
setlocal
cd /d "%~dp0"
set VERSION=1.0.1
set RELEASE=
if /i "%~1"=="release" set RELEASE=1

set CSC=%WINDIR%\Microsoft.NET\Framework64\v4.0.30319\csc.exe
if not exist "%CSC%" set CSC=%WINDIR%\Microsoft.NET\Framework\v4.0.30319\csc.exe
set ICON=
rem The game's icon belongs to its publisher, so it is not used in release builds.
if not defined RELEASE if exist "..\goggame-1207659107.ico" set ICON="/win32icon:..\goggame-1207659107.ico"
"%CSC%" /nologo /target:winexe /optimize+ %ICON% "/out:KKNDHiResolutionMod.exe" /r:System.Windows.Forms.dll /r:System.Drawing.dll KKNDHiResolutionModLauncher.cs || exit /b 1

set VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VSDIR=%%i
if not defined VSDIR (
    echo Visual Studio C++ tools not found - KKNDHiResolutionMod.dll not built.
    exit /b 1
)
set "PATH=%PATH%;%ProgramFiles(x86)%\Microsoft Visual Studio\Installer"
call "%VSDIR%\VC\Auxiliary\Build\vcvars32.bat" >nul
cd /d "%~dp0"
if not exist obj mkdir obj
cl /nologo /O2 /W3 /MT /LD /D_CRT_SECURE_NO_WARNINGS /Foobj\ dll\KKNDHiResolutionMod.c /Fe:KKNDHiResolutionMod.dll /link /DEF:dll\KKNDHiResolutionMod.def /IMPLIB:obj\KKNDHiResolutionMod.lib user32.lib || exit /b 1

if not defined RELEASE exit /b 0

rem Package: the zip holds one KKNDHiResolutionMod folder to extract into the game folder.
set STAGE=release\stage\KKNDHiResolutionMod
if exist release\stage rmdir /s /q release\stage
mkdir "%STAGE%" || exit /b 1
copy /y KKNDHiResolutionMod.exe "%STAGE%\" >nul || exit /b 1
copy /y KKNDHiResolutionMod.dll "%STAGE%\" >nul || exit /b 1
set ZIP=release\KKNDHiResolutionMod-%VERSION%.zip
if exist "%ZIP%" del "%ZIP%"
rem Not Compress-Archive: Windows PowerShell 5.1 writes backslashes into zip entry names.
powershell -NoProfile -Command "Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem; $root = (Resolve-Path 'release\stage').Path; $zip = [IO.Compression.ZipFile]::Open('%ZIP%', 'Create'); try { Get-ChildItem $root -Recurse -File | ForEach-Object { $name = $_.FullName.Substring($root.Length + 1).Replace('\', '/'); [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $_.FullName, $name, 'Optimal') } } finally { $zip.Dispose() }" || exit /b 1
rmdir /s /q release\stage
echo Created %ZIP%
