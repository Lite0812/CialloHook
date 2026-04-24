param(
    [ValidateSet("all","ciallohook","ciallolauncher","runtime")]
    [string]$Target = "all",
    [ValidateSet("Debug","Release")]
    [string]$Configuration = "Release",
    [ValidateSet("x86","x64")]
    [string]$Platform = "x86",
    [ValidateSet("build","clean")]
    [string]$Action = "build"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $root
[Console]::InputEncoding = [System.Text.UTF8Encoding]::new($false)
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
$OutputEncoding = [System.Text.UTF8Encoding]::new($false)

function Remove-IfExists {
    param([string]$Path)
    if (Test-Path $Path) {
        try {
            Remove-Item -Path $Path -Recurse -Force -ErrorAction Stop
        } catch {
            Write-Host "[Warn] Cleanup failed, skipped: $Path"
        }
    }
}

function Sync-HookIni {
    param(
        [string]$Root,
        [string]$Configuration,
        [string]$Platform
    )

    $sourceCialloHookIni = Join-Path $Root "src\CialloHook\config\CialloHook.ini"
    if (-not (Test-Path $sourceCialloHookIni)) {
        throw "Missing config file: $sourceCialloHookIni"
    }

    $buildPlatform = if ($Platform -eq "x86") { "x86" } else { "x64" }
    $targets = @(
        Join-Path $Root ("out\bin\" + $buildPlatform + "\" + $Configuration)
    )

    $extraRuntimeFiles = @(
        (Join-Path $Root "subs_cn_jp.json")
        (Join-Path $Root "third\LE\LoaderDll.dll")
        (Join-Path $Root "third\LE\LocaleEmulator.dll")
    )

    foreach ($targetDir in $targets) {
        if (Test-Path $targetDir) {
            Copy-Item -Path $sourceCialloHookIni -Destination (Join-Path $targetDir "CialloHook.ini") -Force
            Copy-Item -Path $sourceCialloHookIni -Destination (Join-Path $targetDir "version.ini") -Force
            Copy-Item -Path $sourceCialloHookIni -Destination (Join-Path $targetDir "winmm.ini") -Force
            $builtDll = Join-Path $targetDir "CialloHook.dll"
            if (Test-Path $builtDll) {
                Copy-Item -Path $builtDll -Destination (Join-Path $targetDir "version.dll") -Force
                Copy-Item -Path $builtDll -Destination (Join-Path $targetDir "winmm.dll") -Force
                Write-Host "[Info] Output proxy: $targetDir\version.dll"
                Write-Host "[Info] Output proxy: $targetDir\winmm.dll"
            }
            Write-Host "[Info] Output config: $targetDir\CialloHook.ini"
            Write-Host "[Info] Output config: $targetDir\version.ini"
            Write-Host "[Info] Output config: $targetDir\winmm.ini"

            foreach ($extraFile in $extraRuntimeFiles) {
                if (Test-Path $extraFile) {
                    $name = Split-Path -Leaf $extraFile
                    Copy-Item -Path $extraFile -Destination (Join-Path $targetDir $name) -Force
                    Write-Host "[Info] Output extra: $targetDir\$name"
                } else {
                    Write-Host "[Warn] Missing extra file, skipped: $extraFile"
                }
            }
        }
    }
}

function Cleanup-LegacyWin32Obj {
    param([string]$Root)
    $objRoot = Join-Path $Root "out\obj"
    if (-not (Test-Path $objRoot)) {
        return
    }
    Get-ChildItem -Path $objRoot -Directory -Recurse -Filter "Win32" -ErrorAction SilentlyContinue | ForEach-Object {
        Remove-IfExists $_.FullName
    }
}

function Cleanup-Intermediate {
    param([string]$Root)
    Remove-IfExists (Join-Path $Root "Release")
    Remove-IfExists (Join-Path $Root "Debug")
    Remove-IfExists (Join-Path $Root "x64")
    Remove-IfExists (Join-Path $Root "src\RuntimeCore\Debug")
    Remove-IfExists (Join-Path $Root "src\RuntimeCore\Release")
    Remove-IfExists (Join-Path $Root "src\RuntimeCore\x64")
    Remove-IfExists (Join-Path $Root "src\RuntimeCore\RuntimeCore")
    Remove-IfExists (Join-Path $Root "src\CialloHook\Debug")
    Remove-IfExists (Join-Path $Root "src\CialloHook\Release")
    Remove-IfExists (Join-Path $Root "src\CialloHook\x64")
    Remove-IfExists (Join-Path $Root "src\CialloLauncher\Debug")
    Remove-IfExists (Join-Path $Root "src\CialloLauncher\Release")
    Remove-IfExists (Join-Path $Root "src\CialloLauncher\x64")
    Cleanup-LegacyWin32Obj -Root $Root
}

function Find-MSBuild {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $found = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find "MSBuild\**\Bin\MSBuild.exe" | Select-Object -First 1
        if ($found -and (Test-Path $found)) { return $found }
    }

    $fallback = @(
        "E:\VisualStudio\VisualStudio\MSBuild\Current\Bin\MSBuild.exe",
        "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe",
        "C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe",
        "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe"
    )

    foreach ($path in $fallback) {
        if (Test-Path $path) { return $path }
    }

    throw "MSBuild.exe not found"
}

$msbuild = Find-MSBuild
$msTarget = if ($Action -eq "clean") { "Clean;Build" } else { "Build" }
$detoursX64 = Join-Path $root "third\detours\lib.X64\detours.lib"
$detoursX64Alt = Join-Path $root "third\detours\lib.X64\detours_x64.lib"

if ($Platform -eq "x64" -and $Target -ne "runtime" -and -not (Test-Path $detoursX64)) {
    if (Test-Path $detoursX64Alt) {
        Copy-Item -Path $detoursX64Alt -Destination $detoursX64 -Force
    } else {
        throw "Missing third\\detours\\lib.X64\\detours.lib (or detours_x64.lib), cannot build x64 CialloHook/CialloLauncher"
    }
}

$project = $null
if ($Target -eq "ciallohook") { $project = "src\CialloHook\CialloHook.vcxproj" }
if ($Target -eq "ciallolauncher") { $project = "src\CialloLauncher\CialloLauncher.vcxproj" }
if ($Target -eq "runtime") { $project = "src\RuntimeCore\RuntimeCore.vcxproj" }

Write-Host "[Info] MSBuild: $msbuild"
Write-Host "[Info] Target: $Target"
Write-Host "[Info] Configuration: $Configuration"
Write-Host "[Info] Platform: $Platform"
Write-Host "[Info] Action: $msTarget"

function Invoke-Build {
    param([string]$TargetName)
    $msbuildParallelArg = if ($Platform -eq "x86") { "/m:1" } else { "/m" }
    if ($project) {
        $projPlatform = if ($Platform -eq "x86") { "x86" } else { $Platform }
        & $msbuild $project $msbuildParallelArg "/t:$TargetName" "/p:Configuration=$Configuration" "/p:Platform=$projPlatform" "/p:BuildProjectReferences=true" | Out-Host
    } else {
        & $msbuild "CialloHook.sln" $msbuildParallelArg "/t:$TargetName" "/p:Configuration=$Configuration" "/p:Platform=$Platform" | Out-Host
    }
    return [int]$LASTEXITCODE
}

$exitCode = Invoke-Build -TargetName $msTarget
if ($exitCode -ne 0 -and $Action -eq "build") {
    Write-Host "[Warn] Incremental build failed, retrying Clean;Build ..."
    $exitCode = Invoke-Build -TargetName "Clean;Build"
}

if ($exitCode -ne 0) {
    throw "Build failed, exit code=$exitCode"
}

if ($Target -eq "all" -or $Target -eq "ciallohook") {
    Sync-HookIni -Root $root -Configuration $Configuration -Platform $Platform
}

Cleanup-Intermediate -Root $root

Write-Host "[Done] Build succeeded"
exit 0
