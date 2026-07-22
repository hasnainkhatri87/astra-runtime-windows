[CmdletBinding()]
param(
    [ValidatePattern('^[A-Za-z0-9_.]+$')]
    [string]$PackageName = 'com.global.foodpanda.android',

    [ValidateRange(0, 99)]
    [int]$VmIndex = 0,

    [string]$MuMuRoot,

    [switch]$OpenGoogleSignIn,

    [switch]$WaitForInstall,

    [ValidateRange(30, 300)]
    [int]$BootTimeoutSeconds = 120
)

$ErrorActionPreference = 'Stop'

function Find-MuMuRoot {
    if ($MuMuRoot) {
        return [IO.Path]::GetFullPath($MuMuRoot)
    }

    $uninstallKeys = @(
        'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*',
        'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\*'
    )
    $entry = Get-ItemProperty $uninstallKeys -ErrorAction SilentlyContinue |
        Where-Object { $_.DisplayName -eq 'MuMuPlayer' } |
        Select-Object -First 1
    if (-not $entry -or -not $entry.DisplayIcon) {
        throw 'MuMuPlayer was not found. Pass -MuMuRoot with its installation directory.'
    }

    $icon = $entry.DisplayIcon.Trim('"')
    return Split-Path -Parent (Split-Path -Parent $icon)
}

$resolvedRoot = Find-MuMuRoot
$toolRoot = Join-Path $resolvedRoot 'nx_main'
$mumuCli = Join-Path $toolRoot 'mumu-cli.exe'
$adb = Join-Path $toolRoot 'adb.exe'
foreach ($tool in @($mumuCli, $adb)) {
    if (-not (Test-Path -LiteralPath $tool -PathType Leaf)) {
        throw "Required MuMu tool was not found: $tool"
    }
}

function Get-MuMuInfo {
    $raw = & $mumuCli info --vmindex $VmIndex 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "MuMu could not read VM $VmIndex. $($raw -join [Environment]::NewLine)"
    }
    return ($raw | Out-String | ConvertFrom-Json)
}

$info = Get-MuMuInfo
if (-not $info.is_process_started) {
    & $mumuCli control --vmindex $VmIndex launch | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "MuMu could not start VM $VmIndex."
    }
}

$deadline = (Get-Date).AddSeconds($BootTimeoutSeconds)
$serial = $null
do {
    Start-Sleep -Seconds 3
    $info = Get-MuMuInfo
    if ($info.is_android_started -and $info.adb_port) {
        $serial = "127.0.0.1:$($info.adb_port)"
        & $adb connect $serial 2>$null | Out-Null
        $bootCompleted = (& $adb -s $serial shell getprop sys.boot_completed 2>$null).Trim()
        if ($bootCompleted -eq '1') { break }
    }
} while ((Get-Date) -lt $deadline)

if (-not $serial -or $bootCompleted -ne '1') {
    throw "MuMu VM $VmIndex did not finish booting within $BootTimeoutSeconds seconds."
}

& $mumuCli control --vmindex $VmIndex show_window | Out-Null

if ($OpenGoogleSignIn) {
    & $adb -s $serial shell am start -a android.settings.ADD_ACCOUNT_SETTINGS | Out-Null
    Write-Host 'Google account settings opened. Complete authentication yourself in MuMu.'
    exit 0
}

$marketUri = "market://details?id=$PackageName"
& $adb -s $serial shell am start -a android.intent.action.VIEW -d $marketUri -p com.android.vending | Out-Null
if ($LASTEXITCODE -ne 0) {
    throw "Could not open $PackageName in Google Play Store."
}
Write-Host "Opened $PackageName in Google Play Store on MuMu VM $VmIndex."
Write-Host 'This helper never stores or submits Google credentials.'

if (-not $WaitForInstall) { exit 0 }

Write-Host 'Waiting for installation. Press Ctrl+C to stop.'
do {
    Start-Sleep -Seconds 5
    $installed = & $adb -s $serial shell pm path $PackageName 2>$null
} while ($LASTEXITCODE -ne 0 -or -not ($installed -match '^package:'))

& $adb -s $serial shell monkey -p $PackageName -c android.intent.category.LAUNCHER 1 | Out-Null
Write-Host "Installed package detected and launch requested: $PackageName"
