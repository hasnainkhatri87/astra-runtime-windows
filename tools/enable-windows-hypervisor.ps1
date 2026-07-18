[CmdletBinding()]
param([switch]$EnableWindowsHypervisorPlatform)

$ErrorActionPreference = 'Stop'

function Test-Administrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

if (-not $EnableWindowsHypervisorPlatform) {
    throw 'Re-run with -EnableWindowsHypervisorPlatform to explicitly enable the Windows feature. A restart will be required.'
}
if (-not (Test-Administrator)) {
    throw 'Open PowerShell as Administrator, then re-run this script.'
}

$result = Enable-WindowsOptionalFeature -Online -FeatureName HypervisorPlatform -All -NoRestart
$result | Format-List FeatureName,State,RestartNeeded
Write-Host 'Restart Windows before launching the Astra Android VM.'
