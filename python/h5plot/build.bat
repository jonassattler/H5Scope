@echo off
rem SPDX-FileCopyrightText: 2026 Andreas Martin
rem SPDX-License-Identifier: GPL-3.0-only
rem
rem Build h5plot.dll with TDM GCC. The sources live in CMakeLists.txt, which
rem is also what the wheel compiles.
setlocal
set HERE=%~dp0
set ROOT=%HERE%..\..
set CXX=g++
where g++ >nul 2>nul
if errorlevel 1 (
  if exist C:\TDM-GCC-64\bin\g++.exe set "CXX=C:\TDM-GCC-64\bin\g++.exe"
)
for %%I in ("%CXX%") do set "PATH=%%~dpI;%PATH%"
where cmake >nul 2>nul
if errorlevel 1 (
  echo cmake was not found. Install CMake, or install a wheel instead of building the DLL here.
  exit /b 1
)
cmake -S "%HERE%." -B "%HERE%build" -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ -DH5SCOPE_SRC="%ROOT%\src"
if errorlevel 1 exit /b 1
cmake --build "%HERE%build"
if errorlevel 1 exit /b 1
copy /Y "%HERE%build\h5plot.dll" "%HERE%h5plot.dll"
if errorlevel 1 exit /b 1
echo Built %HERE%h5plot.dll
dir "%HERE%h5plot.dll"
