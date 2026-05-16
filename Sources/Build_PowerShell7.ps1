$ErrorActionPreference = "Stop"
$scriptDir = $PSScriptRoot
Set-Location $scriptDir

Write-Host "杰睿 JeriBot - 构建脚本"
Write-Host "请选择要编译的架构："
Write-Host "[1]x64   [2]x86   [3]x64（完整日志）"

$choice = [Console]::ReadKey($true).KeyChar
$VERBOSE = $false
if ($choice -eq "2") {
    Write-Host "已选择架构x86"
    $ARCH = "x86"
    $ARCH_ARG = "Win32"
    $OUT_NAME = "JeriBot_x86.exe"
} elseif ($choice -eq "3") {
    Write-Host "已选择架构x64（完整日志）"
    $ARCH = "x64"
    $ARCH_ARG = "x64"
    $OUT_NAME = "JeriBot_x64.exe"
    $VERBOSE = $true
} else {
    Write-Host "已选择架构x64"
    $ARCH = "x64"
    $ARCH_ARG = "x64"
    $OUT_NAME = "JeriBot_x64.exe"
}

function Die {
    Write-Host "构建失败！" -ForegroundColor Red
    Write-Host "缺少编译环境，请检查 CMake 环境变量、包含 MSVC 的 Visual Studio C++ 桌面开发环境是否存在！"
    Read-Host
    exit 1
}

Write-Host "正在终止相关进程..."
$procs = @(Get-Process -Name "JeriBot_x64","JeriBot_x86" -ErrorAction SilentlyContinue)
if ($procs.Count -gt 0) {
    $procs | Stop-Process -Force
    Start-Sleep -Seconds 1
}

Write-Host "准备构建..."

$CONFIG = "Release"
$ROOT_DIR = $scriptDir
$TEMP_ROOT = Join-Path $scriptDir "..\Debug\Temp"
$BUILD_DIR = Join-Path $TEMP_ROOT "build\$ARCH"
$INSTALL_DIR = Join-Path $TEMP_ROOT "install\$ARCH"
$DIST_ROOT = Join-Path $scriptDir "..\Debug\Export"
$REDIR = if ($VERBOSE) { "" } else { ">`$null 2>&1" }

if (Test-Path $TEMP_ROOT) { Remove-Item -Recurse -Force $TEMP_ROOT -ErrorAction SilentlyContinue }
if (Test-Path $DIST_ROOT) { Remove-Item -Recurse -Force $DIST_ROOT -ErrorAction SilentlyContinue }
New-Item -ItemType Directory -Force $BUILD_DIR | Out-Null
New-Item -ItemType Directory -Force $INSTALL_DIR | Out-Null
New-Item -ItemType Directory -Force $DIST_ROOT | Out-Null

Write-Host "正在编译..."
Invoke-Expression "cmake -S `"$ROOT_DIR`" -B `"$BUILD_DIR`" -A $ARCH_ARG $REDIR"
if ($LASTEXITCODE -ne 0) { Die }
Invoke-Expression "cmake --build `"$BUILD_DIR`" --config $CONFIG $REDIR"
if ($LASTEXITCODE -ne 0) { Die }

Write-Host "正在输出..."
Invoke-Expression "cmake --install `"$BUILD_DIR`" --config $CONFIG --prefix `"$INSTALL_DIR`" $REDIR"
if ($LASTEXITCODE -ne 0) { Die }
Copy-Item "$INSTALL_DIR\JeriBot.exe" (Join-Path $DIST_ROOT $OUT_NAME) -ErrorAction Stop

Write-Host "清理缓存..."
if (Test-Path $TEMP_ROOT) { Remove-Item -Recurse -Force $TEMP_ROOT -ErrorAction SilentlyContinue }

Write-Host "程序输出到：$DIST_ROOT\$OUT_NAME" -ForegroundColor Green
Write-Host "按回车启动 JeriBot"
Read-Host
Start-Process (Join-Path $DIST_ROOT $OUT_NAME)
