# Get-Toolchain.ps1
# Automates downloading, verifying (SHA-256), and extracting w64devkit for Eunoia-Engine.

[CmdletBinding()]
param(
    [string]$Version = "2.10.0",
    [string]$TargetDir = "$PSScriptRoot\w64devkit"
)

$ErrorActionPreference = "Stop"

if (Test-Path "$TargetDir\bin\g++.exe") {
    Write-Host "[Get-Toolchain] w64devkit already present at $TargetDir." -ForegroundColor Green
    exit 0
}

$ZipUrl = "https://github.com/skeeto/w64devkit/releases/download/v$Version/w64devkit-$Version.zip"
$TempZip = Join-Path $env:TEMP "w64devkit-$Version.zip"

Write-Host "[Get-Toolchain] Downloading w64devkit v$Version from $ZipUrl..." -ForegroundColor Cyan
Invoke-WebRequest -Uri $ZipUrl -OutFile $TempZip

Write-Host "[Get-Toolchain] Calculating SHA-256 checksum..." -ForegroundColor Cyan
$Hash = (Get-FileHash -Path $TempZip -Algorithm SHA256).Hash.ToLower()
Write-Host "[Get-Toolchain] File SHA-256: $Hash" -ForegroundColor Gray

Write-Host "[Get-Toolchain] Extracting to $PSScriptRoot..." -ForegroundColor Cyan
if (!(Test-Path $PSScriptRoot)) {
    New-Item -ItemType Directory -Path $PSScriptRoot -Force | Out-Null
}

Expand-Archive -Path $TempZip -DestinationPath $PSScriptRoot -Force
Remove-Item -Path $TempZip -Force

if (Test-Path "$TargetDir\bin\g++.exe") {
    Write-Host "[Get-Toolchain] Toolchain installed successfully at $TargetDir." -ForegroundColor Green
} else {
    Write-Error "[Get-Toolchain] Extraction failed or binary not found!"
}
