param(
    [ValidateSet("Debug","Release")]
    [string]$Configuration = "Release",
    [ValidateSet("x86","x64","both")]
    [string]$Platform = "both",
    [switch]$Clean,
    [switch]$SkipDeps
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $root
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)

# ----------------------------------------------------------------
# 颜色输出
# ----------------------------------------------------------------
function Write-Step  { param([string]$msg) Write-Host "[*] $msg" -ForegroundColor Cyan }
function Write-Ok    { param([string]$msg) Write-Host "[+] $msg" -ForegroundColor Green }
function Write-Warn  { param([string]$msg) Write-Host "[!] $msg" -ForegroundColor Yellow }
function Write-Err   { param([string]$msg) Write-Host "[x] $msg" -ForegroundColor Red }

Write-Host ""
Write-Host "============================================" -ForegroundColor White
Write-Host "  CialloWebM 一键编译" -ForegroundColor White
Write-Host "============================================" -ForegroundColor White
Write-Host ""

# ----------------------------------------------------------------
# 1. 查找 MSBuild
# ----------------------------------------------------------------
function Find-MSBuild {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $found = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild `
                  -find "MSBuild\**\Bin\MSBuild.exe" | Select-Object -First 1
        if ($found -and (Test-Path $found)) { return $found }
    }
    $fallback = @(
        "E:\VisualStudio\VisualStudio\MSBuild\Current\Bin\MSBuild.exe",
        "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe",
        "C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe",
        "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe",
        "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe"
    )
    foreach ($path in $fallback) {
        if (Test-Path $path) { return $path }
    }
    return $null
}

Write-Step "查找 MSBuild ..."
$msbuild = Find-MSBuild
if (-not $msbuild) {
    Write-Err "未找到 MSBuild.exe, 请安装 Visual Studio 2022 (含 C++ 桌面开发工作负载)"
    exit 1
}
Write-Ok "MSBuild: $msbuild"

# ----------------------------------------------------------------
# 2. 检查 & 获取 libvpx
# ----------------------------------------------------------------
$vpxDir      = Join-Path $root "third\libvpx"
$vpxInclude  = Join-Path $vpxDir "include\vpx"
$vpxLibX86   = Join-Path $vpxDir "lib.X86"
$vpxLibX64   = Join-Path $vpxDir "lib.X64"

function Test-LibVpxReady {
    param([string]$Plat)
    $decoder_h = Join-Path $vpxInclude "vpx_decoder.h"
    if (-not (Test-Path $decoder_h)) { return $false }
    $content = Get-Content $decoder_h -Raw -ErrorAction SilentlyContinue
    if ($content -match '#error') { return $false }
    if ($Plat -eq "x86" -or $Plat -eq "both") {
        if (-not (Test-Path (Join-Path $vpxLibX86 "vpx.lib"))) { return $false }
    }
    if ($Plat -eq "x64" -or $Plat -eq "both") {
        if (-not (Test-Path (Join-Path $vpxLibX64 "vpx.lib"))) { return $false }
    }
    return $true
}

function Find-Vcpkg {
    $inPath = Get-Command vcpkg -ErrorAction SilentlyContinue
    if ($inPath) { return $inPath.Source }
    $local = Join-Path $root "vcpkg\vcpkg.exe"
    if (Test-Path $local) { return $local }
    $common = @(
        "C:\vcpkg\vcpkg.exe",
        "C:\src\vcpkg\vcpkg.exe",
        "D:\vcpkg\vcpkg.exe"
    )
    foreach ($p in $common) {
        if (Test-Path $p) { return $p }
    }
    return $null
}

function Install-VcpkgLocal {
    $vcpkgDir = Join-Path $root "vcpkg"
    # 如果上次 clone 失败留下了空目录, 清理掉
    if ((Test-Path $vcpkgDir) -and -not (Test-Path (Join-Path $vcpkgDir "bootstrap-vcpkg.bat"))) {
        Remove-Item -Path $vcpkgDir -Recurse -Force -ErrorAction SilentlyContinue
    }
    if (-not (Test-Path $vcpkgDir)) {
        # 按顺序尝试: GitHub -> 国内镜像
        $urls = @(
            "https://github.com/microsoft/vcpkg.git",
            "https://gitee.com/mirrors/vcpkg.git",
            "https://gitclone.com/github.com/microsoft/vcpkg.git"
        )
        $cloned = $false
        $savedEAP = $ErrorActionPreference
        $ErrorActionPreference = "Continue"
        foreach ($url in $urls) {
            Write-Step "下载 vcpkg ($url) ..."
            & git clone $url $vcpkgDir --depth 1 | Out-Host
            if ($LASTEXITCODE -eq 0) {
                $cloned = $true
                break
            }
            Write-Warn "连接失败, 尝试下一个镜像 ..."
            if (Test-Path $vcpkgDir) {
                Remove-Item -Path $vcpkgDir -Recurse -Force -ErrorAction SilentlyContinue
            }
        }
        $ErrorActionPreference = $savedEAP
        if (-not $cloned) {
            Write-Err "所有镜像均失败, 请手动 git clone vcpkg 或配置代理"
            return $null
        }
    }
    $bootstrap = Join-Path $vcpkgDir "bootstrap-vcpkg.bat"
    $vcpkgExe = Join-Path $vcpkgDir "vcpkg.exe"
    if (-not (Test-Path $vcpkgExe)) {
        Write-Step "编译 vcpkg ..."
        $savedEAP2 = $ErrorActionPreference
        $ErrorActionPreference = "Continue"
        & cmd /c $bootstrap -disableMetrics | Out-Host
        $ErrorActionPreference = $savedEAP2
        if ($LASTEXITCODE -ne 0) {
            Write-Err "vcpkg bootstrap 失败"
            return $null
        }
    }
    return $vcpkgExe
}

function Install-LibVpx {
    param([string]$Plat)

    Write-Step "检查 libvpx 依赖 ..."

    if (Test-LibVpxReady -Plat $Plat) {
        Write-Ok "libvpx 已就绪"
        return $true
    }

    Write-Warn "libvpx 缺失或为占位文件, 尝试通过 vcpkg 自动获取 ..."

    $vcpkg = Find-Vcpkg
    if (-not $vcpkg) {
        $git = Get-Command git -ErrorAction SilentlyContinue
        if (-not $git) {
            Write-Err "未找到 vcpkg 或 git, 无法自动获取 libvpx"
            Write-Host ""
            Write-Host "请手动安装 libvpx:" -ForegroundColor Yellow
            Write-Host "  方法 1: 安装 git, 脚本会自动下载 vcpkg" -ForegroundColor Yellow
            Write-Host "  方法 2: vcpkg install libvpx:x86-windows-static libvpx:x64-windows-static" -ForegroundColor Yellow
            Write-Host "  方法 3: 手动放置头文件和库文件, 详见 third\libvpx\README" -ForegroundColor Yellow
            return $false
        }
        $vcpkg = Install-VcpkgLocal
        if (-not $vcpkg) { return $false }
    }

    Write-Ok "vcpkg: $vcpkg"

    # 自动检测系统代理并设置给 vcpkg
    if (-not $env:HTTP_PROXY) {
        try {
            $reg = Get-ItemProperty "HKCU:\Software\Microsoft\Windows\CurrentVersion\Internet Settings" -ErrorAction SilentlyContinue
            if ($reg.ProxyEnable -and $reg.ProxyServer) {
                $proxy = "http://$($reg.ProxyServer)"
                $env:HTTP_PROXY = $proxy
                $env:HTTPS_PROXY = $proxy
                Write-Step "检测到系统代理: $proxy"
            }
        } catch { }
    }

    $triplets = @()
    if ($Plat -eq "x86" -or $Plat -eq "both") { $triplets += "x86-windows-static" }
    if ($Plat -eq "x64" -or $Plat -eq "both") { $triplets += "x64-windows-static" }

    foreach ($triplet in $triplets) {
        Write-Step "vcpkg install libvpx:$triplet ..."
        $savedEAP3 = $ErrorActionPreference
        $ErrorActionPreference = "Continue"
        & $vcpkg install "libvpx:$triplet" | Out-Host
        $ErrorActionPreference = $savedEAP3
        if ($LASTEXITCODE -ne 0) {
            Write-Err "vcpkg install libvpx:$triplet 失败"
            return $false
        }
    }

    # 复制头文件和库文件到项目目录
    $vcpkgRoot = Split-Path $vcpkg
    $installed = Join-Path $vcpkgRoot "installed"

    # 头文件
    $srcHeaders = $null
    foreach ($triplet in $triplets) {
        $h = Join-Path $installed "$triplet\include\vpx"
        if (Test-Path $h) { $srcHeaders = $h; break }
    }
    if ($srcHeaders) {
        Write-Step "复制头文件 ..."
        if (-not (Test-Path $vpxInclude)) {
            New-Item -ItemType Directory -Path $vpxInclude -Force | Out-Null
        }
        Copy-Item -Path "$srcHeaders\*" -Destination $vpxInclude -Force -Recurse
        Write-Ok "头文件已复制"
    }

    # 库文件
    if ($Plat -eq "x86" -or $Plat -eq "both") {
        $srcLib = Join-Path $installed "x86-windows-static\lib\vpx.lib"
        if (Test-Path $srcLib) {
            if (-not (Test-Path $vpxLibX86)) {
                New-Item -ItemType Directory -Path $vpxLibX86 -Force | Out-Null
            }
            Copy-Item -Path $srcLib -Destination (Join-Path $vpxLibX86 "vpx.lib") -Force
            Write-Ok "x86 库文件已复制"
        }
    }
    if ($Plat -eq "x64" -or $Plat -eq "both") {
        $srcLib = Join-Path $installed "x64-windows-static\lib\vpx.lib"
        if (Test-Path $srcLib) {
            if (-not (Test-Path $vpxLibX64)) {
                New-Item -ItemType Directory -Path $vpxLibX64 -Force | Out-Null
            }
            Copy-Item -Path $srcLib -Destination (Join-Path $vpxLibX64 "vpx.lib") -Force
            Write-Ok "x64 库文件已复制"
        }
    }

    if (Test-LibVpxReady -Plat $Plat) {
        Write-Ok "libvpx 安装完成"
        return $true
    }
    else {
        Write-Err "libvpx 安装后验证失败"
        return $false
    }
}

if (-not $SkipDeps) {
    $depsOk = Install-LibVpx -Plat $Platform
    if (-not $depsOk) { exit 1 }
}
else {
    Write-Warn "跳过依赖检查 (-SkipDeps)"
}

# ----------------------------------------------------------------
# 3. 编译
# ----------------------------------------------------------------
$dllProj    = Join-Path $root "src\CialloWebM\projects\CialloWebM.vcxproj"
$playerProj = Join-Path $root "src\CialloWebM\projects\CialloWebMPlayer.vcxproj"
$buildTarget = "Build"
if ($Clean) { $buildTarget = "Clean;Build" }

function Invoke-MSBuild {
    param([string]$Project, [string]$Plat, [string]$Config)

    $msbuildPlatform = $Plat
    if ($Plat -eq "x86") { $msbuildPlatform = "Win32" }
    $projName = [System.IO.Path]::GetFileNameWithoutExtension($Project)

    Write-Step "编译 $projName ($Plat|$Config) ..."
    $savedEAP4 = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    $slnDir = $root.TrimEnd("\") + "\"
    & $msbuild $Project /t:$buildTarget /p:Configuration=$Config /p:Platform=$msbuildPlatform /p:SolutionDir=$slnDir /p:BuildProjectReferences=false /v:minimal /nologo /m | Out-Host
    $ErrorActionPreference = $savedEAP4
    if ($LASTEXITCODE -ne 0) {
        Write-Err "$projName ($Plat|$Config) 编译失败"
        return $false
    }
    Write-Ok "$projName ($Plat|$Config) 编译成功"
    return $true
}

$platforms = @()
if ($Platform -eq "both") { $platforms = @("x86", "x64") }
else { $platforms = @($Platform) }

$allOk = $true
foreach ($plat in $platforms) {
    $ok = Invoke-MSBuild -Project $dllProj -Plat $plat -Config $Configuration
    if (-not $ok) { $allOk = $false; continue }
    $ok = Invoke-MSBuild -Project $playerProj -Plat $plat -Config $Configuration
    if (-not $ok) { $allOk = $false; continue }
}

# ----------------------------------------------------------------
# 4. 结果
# ----------------------------------------------------------------
Write-Host ""
Write-Host "============================================" -ForegroundColor White
if ($allOk) {
    Write-Host "  编译完成!" -ForegroundColor Green
}
else {
    Write-Host "  部分编译失败" -ForegroundColor Red
}
Write-Host "============================================" -ForegroundColor White
Write-Host ""

foreach ($plat in $platforms) {
    $outDir = Join-Path $root "out\bin\$plat\$Configuration"
    if (Test-Path $outDir) {
        $dll = Join-Path $outDir "ciallo_webm.dll"
        $exe = Join-Path $outDir "ciallo_webm_player.exe"
        if (Test-Path $dll) { Write-Ok "DLL: $dll" }
        if (Test-Path $exe) { Write-Ok "EXE: $exe" }
    }
}

Write-Host ""
if ($allOk) {
    Write-Host "用法:" -ForegroundColor White
    Write-Host "  1. 将 ciallo_webm.dll 放到游戏目录" -ForegroundColor Gray
    Write-Host "  2. CialloHook.ini 中设置 ImageFile = splash.webm" -ForegroundColor Gray
    Write-Host "  3. 测试: ciallo_webm_player.exe splash.webm --loop --drag" -ForegroundColor Gray
}

exit ([int](-not $allOk))






