param(
    [string] $BuildDir = "build-windows",
    [string] $Config = "Release",
    [string] $InnoSetupCompiler = "ISCC.exe"
)

$ErrorActionPreference = "Stop"

$RootDir = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$BuildPath = Join-Path $RootDir $BuildDir
$Vst3Path = Join-Path $BuildPath "BQST_artefacts\$Config\VST3\BQST.vst3"
$InstallerPath = Join-Path $RootDir "dist\BQST-1.0.2-Windows.exe"
$InstallerScript = Join-Path $RootDir "packaging\windows\BQST.iss"

if (-not (Test-Path $Vst3Path)) {
    throw "Missing VST3 artifact: $Vst3Path. Run: cmake --build $BuildDir --config $Config"
}

$Iscc = Get-Command $InnoSetupCompiler -ErrorAction SilentlyContinue
if (-not $Iscc) {
    $DefaultIscc = "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe"
    if (Test-Path $DefaultIscc) {
        $InnoSetupCompiler = $DefaultIscc
    } else {
        throw "Inno Setup compiler not found. Install Inno Setup 6 or pass -InnoSetupCompiler."
    }
}

New-Item -ItemType Directory -Force -Path (Join-Path $RootDir "dist") | Out-Null
& $InnoSetupCompiler $InstallerScript

if (-not (Test-Path $InstallerPath)) {
    throw "Installer was not created: $InstallerPath"
}

Write-Host "Built installer: $InstallerPath"
