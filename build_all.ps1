param(
    [ValidateSet("all","ciallohook","ciallolauncher","runtime","LitePAK_tool","CialloWebM")]
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

    $proxyExportsEnabled = $true
    $buildOptionsHeader = Join-Path $Root "src\CialloHook\config\build_options.h"
    if (Test-Path $buildOptionsHeader) {
        $buildOptionsText = Get-Content -LiteralPath $buildOptionsHeader -Raw -ErrorAction SilentlyContinue
        if ($buildOptionsText -match "(?m)^\s*#define\s+CIALLOHOOK_FEATURE_PROXY_EXPORTS\s+0\b") {
            $proxyExportsEnabled = $false
        }
    }

    $buildPlatform = if ($Platform -eq "x86") { "x86" } else { "x64" }
    $targets = @(
        Join-Path $Root ("out\bin\" + $buildPlatform + "\" + $Configuration)
    )

    $extraRuntimeFiles = [System.Collections.Generic.List[string]]::new()
    $extraRuntimeFiles.Add((Join-Path $Root "subs_cn_jp.json"))

    if ($Platform -eq "x86") {
        $extraRuntimeFiles.Add((Join-Path $Root "third\LE\LoaderDll.dll"))
        $extraRuntimeFiles.Add((Join-Path $Root "third\LE\LocaleEmulator.dll"))
        $extraRuntimeFiles.Add((Join-Path $Root "third\LEP\LoaderDll_x86.dll"))
        $extraRuntimeFiles.Add((Join-Path $Root "third\LEP\LocaleEmulatorPlus_x86.dll"))
    } else {
        $extraRuntimeFiles.Add((Join-Path $Root "third\LEP\LoaderDll_x64.dll"))
        $extraRuntimeFiles.Add((Join-Path $Root "third\LEP\LocaleEmulatorPlus_x64.dll"))
    }

    foreach ($targetDir in $targets) {
        if (Test-Path $targetDir) {
            Copy-Item -Path $sourceCialloHookIni -Destination (Join-Path $targetDir "CialloHook.ini") -Force
            if ($proxyExportsEnabled) {
                Copy-Item -Path $sourceCialloHookIni -Destination (Join-Path $targetDir "version.ini") -Force
                Copy-Item -Path $sourceCialloHookIni -Destination (Join-Path $targetDir "winmm.ini") -Force
            } else {
                Remove-IfExists (Join-Path $targetDir "version.ini")
                Remove-IfExists (Join-Path $targetDir "winmm.ini")
            }
            $builtDll = Join-Path $targetDir "CialloHook.dll"
            if ($proxyExportsEnabled -and (Test-Path $builtDll)) {
                Copy-Item -Path $builtDll -Destination (Join-Path $targetDir "version.dll") -Force
                Copy-Item -Path $builtDll -Destination (Join-Path $targetDir "winmm.dll") -Force
            } elseif (-not $proxyExportsEnabled) {
                Remove-IfExists (Join-Path $targetDir "version.dll")
                Remove-IfExists (Join-Path $targetDir "winmm.dll")
            }

            foreach ($extraFile in $extraRuntimeFiles) {
                if (Test-Path $extraFile) {
                    $name = Split-Path -Leaf $extraFile
                    Copy-Item -Path $extraFile -Destination (Join-Path $targetDir $name) -Force
                } else {
                    Write-Host "[Warn] Missing extra file, skipped: $extraFile"
                }
            }

            # Copy ciallo_webm.dll if it was built
            $webmDll = Join-Path $targetDir "ciallo_webm.dll"
            if (Test-Path $webmDll) {
            } else {
                Write-Host "[Warn] ciallo_webm.dll not found in output, WebM splash will not be available"
            }
        }
    }
}

function Split-OutputArtifacts {
    param(
        [string]$Root,
        [string]$Configuration,
        [string]$Platform
    )

    $buildPlatform = if ($Platform -eq "x86") { "x86" } else { "x64" }
    $targetDir = Join-Path $Root ("out\bin\" + $buildPlatform + "\" + $Configuration)
    if (-not (Test-Path $targetDir)) {
        return
    }

    $extraDir = Join-Path $targetDir "_extra"
    $keepExtensions = @(".exe", ".dll", ".ini", ".json")
    $movedCount = 0

    if (Test-Path $extraDir) {
        Get-ChildItem -Path $extraDir -File -ErrorAction SilentlyContinue | ForEach-Object {
            $extension = $_.Extension.ToLowerInvariant()
            if ($keepExtensions -contains $extension) {
                Move-Item -Path $_.FullName -Destination (Join-Path $targetDir $_.Name) -Force
            }
        }
    }

    Get-ChildItem -Path $targetDir -File -ErrorAction SilentlyContinue | ForEach-Object {
        $extension = $_.Extension.ToLowerInvariant()
        if ($keepExtensions -contains $extension) {
            return
        }

        if (-not (Test-Path $extraDir)) {
            New-Item -ItemType Directory -Path $extraDir -Force | Out-Null
        }

        Move-Item -Path $_.FullName -Destination (Join-Path $extraDir $_.Name) -Force
        $movedCount++
    }

    if ($movedCount -eq 0 -and (Test-Path $extraDir)) {
        $remainingFiles = Get-ChildItem -Path $extraDir -File -ErrorAction SilentlyContinue
        $remainingDirs = Get-ChildItem -Path $extraDir -Directory -ErrorAction SilentlyContinue
        if (@($remainingFiles).Count -eq 0 -and @($remainingDirs).Count -eq 0) {
            Remove-IfExists $extraDir
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

if ($Platform -eq "x64" -and @("all","ciallohook","ciallolauncher") -contains $Target -and -not (Test-Path $detoursX64)) {
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
if ($Target -eq "LitePAK_tool") { $project = "src\LitePAK_tool\projects\LitePAK_tool.vcxproj" }
if ($Target -eq "CialloWebM") { $project = "src\CialloWebM\projects\CialloWebM.vcxproj" }

function Invoke-Build {
    param([string]$TargetName)
    $msbuildParallelArg = if ($Platform -eq "x86") { "/m:1" } else { "/m" }
    if ($project) {
        $projPlatform = if ($Platform -eq "x86") { "Win32" } else { $Platform }
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

Split-OutputArtifacts -Root $root -Configuration $Configuration -Platform $Platform

Cleanup-Intermediate -Root $root

Write-Host "[Done] Build succeeded"
exit 0
