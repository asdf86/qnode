@echo off
REM QNode Build Script for Windows

set BUILD_DIR=%BUILD_DIR:build%
if "%BUILD_DIR%"=="" set BUILD_DIR=build
set BUILD_TYPE=%BUILD_TYPE:Release%
if "%BUILD_TYPE%"=="" set BUILD_TYPE=Release

echo QNode Build Script
echo ==================
echo Build directory: %BUILD_DIR%
echo Build type: %BUILD_TYPE%

REM Create build directory
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
cd /d "%BUILD_DIR%"

REM Configure with CMake
echo Configuring with CMake...
cmake -DCMAKE_BUILD_TYPE="%BUILD_TYPE%" ..

REM Build
echo Building...
cmake --build . --config "%BUILD_TYPE%"

echo.
echo Build complete!
echo Executable: bin\qnode.exe
echo.
echo To test the build, run:
echo   bin\qnode.exe ..\test_simple.js

pause
