param(
    [Alias('KeilPath')]
    [string]$Uv4Path = $env:KEIL_UV4_PATH,
    [switch]$Rebuild
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$projectPath = Join-Path $projectRoot 'MDK-ARM\DM_MC02_BusScope.uvprojx'
$logPath = Join-Path $projectRoot 'MDK-ARM\build.log'

if (-not $Uv4Path) {
    $command = Get-Command UV4.exe -ErrorAction SilentlyContinue
    if ($command) {
        $Uv4Path = $command.Source
    }
}

if (-not $Uv4Path) {
    $registryPaths = @(
        'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\*',
        'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*',
        'HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*'
    )
    $installations = Get-ItemProperty $registryPaths -ErrorAction SilentlyContinue |
        Where-Object { $_.DisplayName -like 'Keil*' -and $_.DisplayIcon }
    foreach ($installation in $installations) {
        $candidate = ($installation.DisplayIcon -replace ',\d+$', '').Trim('"')
        if ((Split-Path -Leaf $candidate) -ieq 'UV4.exe' -and
            (Test-Path -LiteralPath $candidate -PathType Leaf)) {
            $Uv4Path = $candidate
            break
        }
    }
}

if (-not $Uv4Path -or -not (Test-Path -LiteralPath $Uv4Path -PathType Leaf)) {
    throw 'Keil UV4.exe was not found. Pass -Uv4Path or set KEIL_UV4_PATH.'
}

$Uv4Path = (Resolve-Path -LiteralPath $Uv4Path).Path
$buildMode = if ($Rebuild) { '-r' } else { '-b' }
$arguments = @($buildMode, ('"{0}"' -f $projectPath), '-t',
    'DM_MC02_BusScope', '-o', ('"{0}"' -f $logPath))
Write-Host ('Building with {0}' -f $Uv4Path)
$buildStartedUtc = [DateTime]::UtcNow
$process = Start-Process -FilePath $Uv4Path -ArgumentList $arguments `
    -WorkingDirectory (Split-Path -Parent $projectPath) -WindowStyle Hidden -PassThru
$process.WaitForExit()
$process.Refresh()

if (-not (Test-Path -LiteralPath $logPath)) {
    throw 'Keil exited without creating a build log.'
}
if ((Get-Item -LiteralPath $logPath).LastWriteTimeUtc -lt $buildStartedUtc) {
    throw 'Keil did not update the build log; the previous log cannot verify this build.'
}
$buildLog = Get-Content -LiteralPath $logPath -Raw
Write-Output $buildLog

# uVision returns 1 for a successful build with warnings, 2+ for errors.
if ($process.ExitCode -notin @(0, 1)) {
    throw ('Keil build failed with exit code {0}. See {1}' -f $process.ExitCode, $logPath)
}

if ($buildLog -notmatch ' - 0 Error\(s\), \d+ Warning\(s\)\.') {
    throw ('Keil did not report a successful build. See {0}' -f $logPath)
}
Write-Host ('Build completed. Log: {0}' -f $logPath)
