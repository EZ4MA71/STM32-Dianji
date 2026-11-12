[CmdletBinding()]
param(
    [switch]$Rebuild,
    [string]$UV4Path = $env:KEIL_UV4_PATH
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$projectFile = Join-Path $projectRoot 'MDK-ARM\foc.uvprojx'
$outputDirectory = Join-Path $projectRoot 'MDK-ARM\Objects'
$logFile = Join-Path $outputDirectory 'build.log'

if (-not $UV4Path) {
    foreach ($candidate in @(
        'D:\work\keil_v5\UV4\UV4.exe',
        'C:\Keil_v5\UV4\UV4.exe',
        'D:\Keil_v5\UV4\UV4.exe'
    )) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            $UV4Path = $candidate
            break
        }
    }
}
if (-not $UV4Path -or -not (Test-Path -LiteralPath $UV4Path -PathType Leaf)) {
    throw 'Keil UV4.exe not found. Set KEIL_UV4_PATH or pass -UV4Path.'
}

New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
if (Test-Path -LiteralPath $logFile) {
    Remove-Item -LiteralPath $logFile
}
$buildOption = if ($Rebuild) { '-r' } else { '-b' }
$arguments = @($buildOption, ('"' + $projectFile + '"'), '-t', '"STM32H743VIT6_FOC"', '-j0', '-o', ('"' + $logFile + '"'))
$process = Start-Process -FilePath $UV4Path -ArgumentList $arguments -WorkingDirectory (Join-Path $projectRoot 'MDK-ARM') -WindowStyle Hidden -Wait -PassThru

$logText = if (Test-Path -LiteralPath $logFile) { Get-Content -LiteralPath $logFile -Raw } else { '' }
Write-Output $logText
# uVision: 0 = success, 1 = warnings, 2+ = errors. A fresh log must confirm success.
if ($process.ExitCode -gt 1 -or $logText -notmatch '0 Error\(s\)' -or
    -not (Test-Path -LiteralPath (Join-Path $outputDirectory 'foc.axf')) -or
    -not (Test-Path -LiteralPath (Join-Path $outputDirectory 'foc.hex'))) {
    throw "Keil build failed (exit code $($process.ExitCode)). See $logFile"
}
