# 生成工时簿更新包并写入更新清单。
# 用法示例:
#   powershell -File scripts/make_update_package.ps1 `
#       -SourceDir "out/build/vs2022-RelWithDebInfo/RelWithDebInfo" `
#       -Version "v2026.09.07" -Notes "修复若干问题。" `
#       -UpdatesDir "D:/AttendanceUpdates"
# 产物:
#   <UpdatesDir>/packages/AttendanceApp-<Version>.zip
#   <UpdatesDir>/manifest.json
param(
    [Parameter(Mandatory = $true)][string]$SourceDir,
    [Parameter(Mandatory = $true)][string]$Version,
    [string]$Notes = "",
    [Parameter(Mandatory = $true)][string]$UpdatesDir,
    [string]$WindeployQtPath = "",
    [string]$IsccPath = "",
    [string]$VcpkgBinDir = "",
    [switch]$SkipInstaller
)

$ErrorActionPreference = "Stop"

function Resolve-ToolPath {
    param(
        [string]$ConfiguredPath,
        [string]$CommandName,
        [string]$FriendlyName
    )

    if (-not [string]::IsNullOrWhiteSpace($ConfiguredPath)) {
        if (Test-Path -LiteralPath $ConfiguredPath -PathType Leaf) {
            return (Resolve-Path -LiteralPath $ConfiguredPath).Path
        }
        throw "$FriendlyName 路径无效: $ConfiguredPath"
    }

    $command = Get-Command $CommandName -ErrorAction SilentlyContinue
    if ($null -ne $command) {
        return $command.Source
    }

    if ($CommandName -ieq "ISCC.exe") {
        $knownPaths = @(
            (Join-Path ${env:ProgramFiles(x86)} "Inno Setup 6\ISCC.exe"),
            (Join-Path $env:LOCALAPPDATA "Programs\Inno Setup 6\ISCC.exe")
        )
        foreach ($knownPath in $knownPaths) {
            if (Test-Path -LiteralPath $knownPath -PathType Leaf) {
                return $knownPath
            }
        }
    }
    throw "找不到 $FriendlyName。请将其加入 PATH，或通过参数指定路径。"
}

function Initialize-VisualStudioRuntime {
    if (-not [string]::IsNullOrWhiteSpace($env:VCINSTALLDIR)) {
        return
    }

    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) {
        return
    }

    $installPath = (& $vswhere -latest -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath | Select-Object -First 1).Trim()
    $vcInstallPath = Join-Path $installPath "VC"
    if ((-not [string]::IsNullOrWhiteSpace($installPath)) -and (Test-Path -LiteralPath $vcInstallPath -PathType Container)) {
        $env:VCINSTALLDIR = $vcInstallPath + "\"
    }
}

function Resolve-VcpkgRuntimeDir {
    param([string]$ConfiguredPath)

    $candidates = @($ConfiguredPath)
    if (-not [string]::IsNullOrWhiteSpace($env:VCPKG_ROOT)) {
        $candidates += (Join-Path $env:VCPKG_ROOT "installed\x64-windows\bin")
    }
    $candidates += "D:\vcpkg\installed\x64-windows\bin"

    foreach ($candidate in $candidates) {
        if (-not [string]::IsNullOrWhiteSpace($candidate) -and
            (Test-Path -LiteralPath (Join-Path $candidate "drogon.dll") -PathType Leaf)) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }
    throw "找不到 Drogon 运行时目录。请通过 -VcpkgBinDir 指定包含 drogon.dll 的目录。"
}

if (-not (Test-Path (Join-Path $SourceDir "AttendanceApp.exe"))) {
    throw "SourceDir 下找不到 AttendanceApp.exe: $SourceDir"
}
if (-not (Test-Path (Join-Path $SourceDir "AttendanceUpdateService.exe"))) {
    throw "SourceDir 下找不到 AttendanceUpdateService.exe: $SourceDir"
}
if ($Version -notmatch '^v\d{4}\.\d{2}\.\d{2}$') {
    throw "版本号必须为 vYYYY.MM.DD，例如 v2026.09.07: $Version"
}

$packageDir = Join-Path $UpdatesDir "packages"
$installerDir = Join-Path $UpdatesDir "installers"
New-Item -ItemType Directory -Force -Path $packageDir | Out-Null
New-Item -ItemType Directory -Force -Path $installerDir | Out-Null

# 客户端和更新服务分别准备运行时目录，避免互相混入发布包。
$clientStaging = Join-Path ([System.IO.Path]::GetTempPath()) ("AttendanceApp-release-" + [guid]::NewGuid().ToString("N"))
$serviceStaging = Join-Path ([System.IO.Path]::GetTempPath()) ("AttendanceUpdateService-release-" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Force -Path $clientStaging | Out-Null
New-Item -ItemType Directory -Force -Path $serviceStaging | Out-Null
$installerPath = ""
$serviceInstallerPath = ""
try {
    Initialize-VisualStudioRuntime
    $windeployqt = Resolve-ToolPath $WindeployQtPath "windeployqt.exe" "windeployqt"

    Copy-Item -LiteralPath (Join-Path $SourceDir "AttendanceApp.exe") -Destination $clientStaging -Force
    Copy-Item -Path (Join-Path $SourceDir "*.dll") -Destination $clientStaging -Force
    & $windeployqt --release --compiler-runtime --no-translations --dir $clientStaging `
        (Join-Path $clientStaging "AttendanceApp.exe")
    if ($LASTEXITCODE -ne 0) {
        throw "客户端 windeployqt 执行失败，退出码: $LASTEXITCODE"
    }

    Copy-Item -LiteralPath (Join-Path $SourceDir "AttendanceUpdateService.exe") -Destination $serviceStaging -Force
    $vcpkgRuntimeDir = Resolve-VcpkgRuntimeDir $VcpkgBinDir
    $serviceRuntimeDlls = @(
        "drogon.dll", "trantor.dll", "jsoncpp.dll", "cares.dll", "brotlicommon.dll",
        "brotlidec.dll", "brotlienc.dll", "zlib1.dll", "libcrypto-3-x64.dll", "libssl-3-x64.dll"
    )
    foreach ($runtimeDll in $serviceRuntimeDlls) {
        $runtimePath = Join-Path $vcpkgRuntimeDir $runtimeDll
        if (-not (Test-Path -LiteralPath $runtimePath -PathType Leaf)) {
            throw "缺少服务运行时 DLL: $runtimePath"
        }
        Copy-Item -LiteralPath $runtimePath -Destination $serviceStaging -Force
    }
    # 更新服务的 OpenSSL 必须与 Drogon 同源，不能被客户端构建目录中的旧 DLL 覆盖。
    & $windeployqt --release --compiler-runtime --no-translations --dir $serviceStaging `
        (Join-Path $serviceStaging "AttendanceUpdateService.exe")
    if ($LASTEXITCODE -ne 0) {
        throw "服务端 windeployqt 执行失败，退出码: $LASTEXITCODE"
    }

    $zipPath = Join-Path $packageDir ("AttendanceApp-{0}.zip" -f $Version)
    if (Test-Path $zipPath) {
        Remove-Item -LiteralPath $zipPath -Force
    }
    # VC 运行库安装器只服务于首次安装，客户端自更新无需重复下载和执行它。
    $updateItems = Get-ChildItem -LiteralPath $clientStaging -Force |
        Where-Object { $_.Name -ne "vc_redist.x64.exe" }
    Compress-Archive -Path $updateItems.FullName -DestinationPath $zipPath -CompressionLevel Optimal

    if (-not $SkipInstaller) {
        $iscc = Resolve-ToolPath $IsccPath "ISCC.exe" "Inno Setup Compiler (ISCC.exe)"
        $installerScript = Join-Path $PSScriptRoot "..\installer\AttendanceApp.iss"
        & $iscc "/DAppVersion=$Version" "/DSourceDir=$clientStaging" "/DOutputDir=$installerDir" $installerScript
        if ($LASTEXITCODE -ne 0) {
            throw "Inno Setup 编译失败，退出码: $LASTEXITCODE"
        }
        $installerPath = Join-Path $installerDir ("AttendanceApp-Setup-{0}.exe" -f $Version)
        if (-not (Test-Path -LiteralPath $installerPath -PathType Leaf)) {
            throw "Inno Setup 未生成预期安装程序: $installerPath"
        }

        $serviceInstallerScript = Join-Path $PSScriptRoot "..\installer\AttendanceUpdateService.iss"
        & $iscc "/DAppVersion=$Version" "/DSourceDir=$serviceStaging" "/DOutputDir=$installerDir" $serviceInstallerScript
        if ($LASTEXITCODE -ne 0) {
            throw "更新服务 Inno Setup 编译失败，退出码: $LASTEXITCODE"
        }
        $serviceInstallerPath = Join-Path $installerDir ("AttendanceUpdateService-Setup-{0}.exe" -f $Version)
        if (-not (Test-Path -LiteralPath $serviceInstallerPath -PathType Leaf)) {
            throw "Inno Setup 未生成预期更新服务安装程序: $serviceInstallerPath"
        }
    }
}
finally {
    Remove-Item -LiteralPath $clientStaging -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $serviceStaging -Recurse -Force -ErrorAction SilentlyContinue
}

$hash = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash.ToLowerInvariant()
$manifest = [ordered]@{
    available   = $true
    version     = $Version
    publishedAt = (Get-Date).ToString("yyyy-MM-dd HH:mm")
    notes       = $Notes
    downloadUrl = ("/packages/AttendanceApp-{0}.zip" -f $Version)
    sha256      = $hash
}
if (-not [string]::IsNullOrWhiteSpace($installerPath)) {
    $manifest.installerUrl = ("/installers/AttendanceApp-Setup-{0}.exe" -f $Version)
}
if (-not [string]::IsNullOrWhiteSpace($serviceInstallerPath)) {
    $manifest.serviceInstallerUrl = ("/installers/AttendanceUpdateService-Setup-{0}.exe" -f $Version)
}
$manifestPath = Join-Path $UpdatesDir "manifest.json"
$manifest | ConvertTo-Json | Set-Content -LiteralPath $manifestPath -Encoding UTF8

Write-Output ("更新包: " + $zipPath)
if (-not [string]::IsNullOrWhiteSpace($installerPath)) {
    Write-Output ("安装包: " + $installerPath)
}
if (-not [string]::IsNullOrWhiteSpace($serviceInstallerPath)) {
    Write-Output ("服务安装包: " + $serviceInstallerPath)
}
Write-Output ("清单  : " + $manifestPath)
Write-Output ("SHA256: " + $hash)
