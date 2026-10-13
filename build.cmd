@echo off
rem Builds "KKND Resolution Launcher.exe" with the C# compiler that ships with Windows (.NET Framework 4).
setlocal
cd /d "%~dp0"
set CSC=%WINDIR%\Microsoft.NET\Framework64\v4.0.30319\csc.exe
if not exist "%CSC%" set CSC=%WINDIR%\Microsoft.NET\Framework\v4.0.30319\csc.exe
set ICON=
if exist "..\goggame-1207659107.ico" set ICON="/win32icon:..\goggame-1207659107.ico"
"%CSC%" /nologo /target:winexe /optimize+ %ICON% "/out:KKND Resolution Launcher.exe" /r:System.Windows.Forms.dll /r:System.Drawing.dll KKNDResLauncher.cs
