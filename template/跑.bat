@echo off
rem Compile and run. Run start.bat in the toolbox first (it puts cmake on PATH).
rem Extra args go to app, e.g.  run.bat --seed 7   /   run.bat --debug
setlocal
cd /d "%~dp0"

where cmake >nul 2>&1
if errorlevel 1 goto :nocmake

set "EXTRA="
if exist "..\easel\CMakeLists.txt" set "EXTRA=-DEASEL_DIR=..\easel"

if not exist "build\mingw\CMakeCache.txt" (
    echo [1/2] 第一次运行，先配置...
    cmake --preset mingw %EXTRA%
    if errorlevel 1 goto :fail
)

echo [2/2] 编译...
cmake --build --preset mingw
if errorlevel 1 goto :fail

echo.
"build\mingw\bin\app.exe" --open data\example.json --solve %*
goto :end

:nocmake
echo.
echo 找不到 cmake。
echo 请先双击工具箱根目录里的 start.bat（或 启动.bat），
echo 在它打开的那个命令行窗口里再运行本脚本。
goto :end

:fail
echo.
echo 构建失败。上面的报错是关键。可以试试：
echo   1. 删掉 build 目录重来
echo   2. 在命令行里跑 cmake --version 和 g++ --version 看看是不是都能找到
echo   3. 跑 build\mingw\bin\app.exe --doctor 看环境自检

:end
echo.
pause
endlocal
