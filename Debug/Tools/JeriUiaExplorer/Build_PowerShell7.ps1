$ErrorActionPreference = "Stop"
$scriptDir = $PSScriptRoot
Set-Location $scriptDir

Write-Host "JeriUia - 构建脚本"
Write-Host "请选择要编译的架构："
Write-Host "[1]x64   [2]x86"

$choice = [Console]::ReadKey($true).KeyChar
if ($choice -eq "2") {
    Write-Host "已选择架构x86"
    $ARCH = "x86"
    $ARCH_ARG = "Win32"
    $OUT_NAME = "JeriUiaExplorer_x86.exe"
} else {
    Write-Host "已选择架构x64"
    $ARCH = "x64"
    $ARCH_ARG = "x64"
    $OUT_NAME = "JeriUiaExplorer_x64.exe"
}

Write-Host "准备构建..."

$CONFIG = "Release"
$ROOT_DIR = $scriptDir
$TEMP_ROOT = Join-Path $scriptDir "temp"
$BUILD_DIR = Join-Path $TEMP_ROOT "build\$ARCH"
$INSTALL_DIR = Join-Path $TEMP_ROOT "install\$ARCH"
$DIST_ROOT = Join-Path $scriptDir "dist"

if (Test-Path $TEMP_ROOT) { Remove-Item -Recurse -Force $TEMP_ROOT -ErrorAction SilentlyContinue }
if (Test-Path $DIST_ROOT) { Remove-Item -Recurse -Force $DIST_ROOT -ErrorAction SilentlyContinue }
New-Item -ItemType Directory -Force $BUILD_DIR | Out-Null
New-Item -ItemType Directory -Force $INSTALL_DIR | Out-Null
New-Item -ItemType Directory -Force $DIST_ROOT | Out-Null

Write-Host "正在编译..."
cmake -S $ROOT_DIR -B $BUILD_DIR -A $ARCH_ARG >$null 2>&1
if ($LASTEXITCODE -ne 0) { Write-Host "构建失败！"; Write-Host "缺少编译环境，请检查 CMake 环境变量、包含 MSVC 的 Visual Studio C++ 桌面开发环境是否存在！"; Read-Host; exit 1 }
cmake --build $BUILD_DIR --config $CONFIG >$null 2>&1
if ($LASTEXITCODE -ne 0) { Write-Host "构建失败！"; Write-Host "缺少编译环境，请检查 CMake 环境变量、包含 MSVC 的 Visual Studio C++ 桌面开发环境是否存在！"; Read-Host; exit 1 }

Write-Host "正在输出..."
cmake --install $BUILD_DIR --config $CONFIG --prefix $INSTALL_DIR >$null 2>&1
if ($LASTEXITCODE -ne 0) { Write-Host "构建失败！"; Write-Host "缺少编译环境，请检查 CMake 环境变量、包含 MSVC 的 Visual Studio C++ 桌面开发环境是否存在！"; Read-Host; exit 1 }
Copy-Item "$INSTALL_DIR\JeriUiaExplorer.exe" (Join-Path $DIST_ROOT $OUT_NAME) -ErrorAction Stop

Write-Host "清理缓存..."
if (Test-Path $TEMP_ROOT) { Remove-Item -Recurse -Force $TEMP_ROOT -ErrorAction SilentlyContinue }

Write-Host "程序输出到：$DIST_ROOT\$OUT_NAME"
Write-Host "已完成构建，按回车退出"
Read-Host
exit 0
