param(
    [string]$FfmpegTag = "n7.1.1",
    [string]$MsysRoot = "D:\Software\msys64",
    [string]$BuildRoot = "build\ffmpeg-min",
    [string]$InstallDir = "third_party\ffmpeg",
    [switch]$PublishToVendor
)

$ErrorActionPreference = "Stop"

function Assert-PathExists {
    param([string]$PathValue, [string]$Name)
    if (-not (Test-Path $PathValue)) {
        throw "$Name not found: $PathValue"
    }
}

function Import-VsDevEnvironment {
    $vcvars = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
    Assert-PathExists -PathValue $vcvars -Name "vcvars64.bat"

    $tmpCmd = Join-Path $env:TEMP "snappin_capture_env.cmd"
    @"
@echo off
call "$vcvars" >nul
set
"@ | Set-Content -Path $tmpCmd -Encoding ASCII

    & "C:\Windows\System32\cmd.exe" /c $tmpCmd | ForEach-Object {
        if ($_ -match '^(.*?)=(.*)$') {
            Set-Item -Path ("Env:" + $matches[1]) -Value $matches[2]
        }
    }
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$msysBash = Join-Path $MsysRoot "usr\bin\bash.exe"
$msysUsrBin = Join-Path $MsysRoot "usr\bin"
$msysUcrtBin = Join-Path $MsysRoot "ucrt64\bin"
$buildRootAbs = (Resolve-Path (Join-Path $repoRoot ".")).Path + "\" + $BuildRoot
$srcDir = Join-Path $buildRootAbs "ffmpeg-src"
$installAbs = (Resolve-Path (Join-Path $repoRoot ".")).Path + "\" + $InstallDir
$vendorDir = Join-Path $repoRoot "third_party\ffmpeg"
$configureScriptAbs = Join-Path $repoRoot "tools\ffmpeg\configure_minimal_msvc.sh"

Assert-PathExists -PathValue $msysBash -Name "MSYS2 bash"
Assert-PathExists -PathValue $configureScriptAbs -Name "configure_minimal_msvc.sh"

Import-VsDevEnvironment

$env:PATH = "$msysUcrtBin;$msysUsrBin;$env:PATH"
$env:MSYSTEM = "MSYS"
$env:CHERE_INVOKING = "1"
$env:MSYS2_PATH_TYPE = "inherit"

New-Item -ItemType Directory -Path $buildRootAbs -Force | Out-Null

if (-not (Test-Path $srcDir)) {
    & git clone --depth 1 --branch $FfmpegTag https://git.ffmpeg.org/ffmpeg.git $srcDir
}

$srcPosix = (& $msysBash -lc "cygpath -u '$srcDir'").Trim()
$installPosix = (& $msysBash -lc "cygpath -u '$installAbs'").Trim()
$scriptPosix = (& $msysBash -lc "cygpath -u '$configureScriptAbs'").Trim()

& $msysBash -lc "set -euo pipefail; '$scriptPosix' '$srcPosix' '$installPosix'; cd '$srcPosix'; make -j8; make install"

$libMap = @(
    @{ Src = Join-Path $srcDir "libavcodec\avcodec.lib"; Dst = Join-Path $installAbs "lib\avcodec.lib" },
    @{ Src = Join-Path $srcDir "libavformat\avformat.lib"; Dst = Join-Path $installAbs "lib\avformat.lib" },
    @{ Src = Join-Path $srcDir "libavutil\avutil.lib"; Dst = Join-Path $installAbs "lib\avutil.lib" },
    @{ Src = Join-Path $srcDir "libswscale\swscale.lib"; Dst = Join-Path $installAbs "lib\swscale.lib" }
)
foreach ($pair in $libMap) {
    Assert-PathExists -PathValue $pair.Src -Name "import library"
    Copy-Item -Path $pair.Src -Destination $pair.Dst -Force
}

@("share", "doc", "presets") | ForEach-Object {
    $dir = Join-Path $installAbs $_
    if (Test-Path $dir) {
        Remove-Item -Path $dir -Recurse -Force
    }
}
Get-ChildItem (Join-Path $installAbs "bin\*.lib") -ErrorAction SilentlyContinue | Remove-Item -Force

if ($PublishToVendor) {
    $vendorResolved = $null
    if (Test-Path $vendorDir) {
        $vendorResolved = (Resolve-Path $vendorDir).Path
    }
    if ($vendorResolved -and (Resolve-Path $installAbs).Path -eq $vendorResolved) {
        Write-Host "Install dir already equals vendor dir, skip extra publish step."
        exit 0
    }
    if (Test-Path $vendorDir) {
        Remove-Item -Path $vendorDir -Recurse -Force
    }
    Copy-Item -Path $installAbs -Destination $vendorDir -Recurse -Force
}

Write-Host "Minimal FFmpeg build ready:"
Write-Host "  install = $installAbs"
if ($PublishToVendor) {
    Write-Host "  vendor  = $vendorDir"
}
