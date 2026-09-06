# 生成工时簿更新包并写入更新清单。
# 用法示例:
#   powershell -File scripts/make_update_package.ps1 `
#       -SourceDir "out/build/vs2022-RelWithDebInfo/RelWithDebInfo" `
#       -Version "1.1.0" -Notes "修复若干问题。" `
#       -UpdatesDir "D:/AttendanceUpdates"
# 产物:
#   <UpdatesDir>/packages/AttendanceApp-<Version>.zip
#   <UpdatesDir>/manifest.json
param(
    [Parameter(Mandatory = $true)][string]$SourceDir,
    [Parameter(Mandatory = $true)][string]$Version,
    [string]$Notes = "",
    [Parameter(Mandatory = $true)][string]$UpdatesDir
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path (Join-Path $SourceDir "AttendanceApp.exe"))) {
    throw "SourceDir 下找不到 AttendanceApp.exe: $SourceDir"
}
if ($Version -notmatch '^\d+(\.\d+){2,}$') {
    throw "版本号必须为点分数字，如 1.1.0: $Version"
}

$packageDir = Join-Path $UpdatesDir "packages"
New-Item -ItemType Directory -Force -Path $packageDir | Out-Null

# 需要随包分发的运行时文件（exe + 全部 DLL）。
$staging = Join-Path ([System.IO.Path]::GetTempPath()) ("AttendanceApp-pkg-" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Force -Path $staging | Out-Null
try {
    Copy-Item -Path (Join-Path $SourceDir "*.exe") -Destination $staging -Force
    Copy-Item -Path (Join-Path $SourceDir "*.dll") -Destination $staging -Force

    $zipPath = Join-Path $packageDir ("AttendanceApp-{0}.zip" -f $Version)
    if (Test-Path $zipPath) {
        Remove-Item -LiteralPath $zipPath -Force
    }
    Compress-Archive -Path (Join-Path $staging "*") -DestinationPath $zipPath -CompressionLevel Optimal
}
finally {
    Remove-Item -LiteralPath $staging -Recurse -Force -ErrorAction SilentlyContinue
}

$hash = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash.ToLowerInvariant()
$manifest = [ordered]@{
    available   = $true
    version     = $Version
    notes       = $Notes
    downloadUrl = ("/packages/AttendanceApp-{0}.zip" -f $Version)
    sha256      = $hash
}
$manifestPath = Join-Path $UpdatesDir "manifest.json"
$manifest | ConvertTo-Json | Set-Content -LiteralPath $manifestPath -Encoding UTF8

Write-Output ("更新包: " + $zipPath)
Write-Output ("清单  : " + $manifestPath)
Write-Output ("SHA256: " + $hash)
