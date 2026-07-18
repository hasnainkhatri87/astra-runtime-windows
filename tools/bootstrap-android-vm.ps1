[CmdletBinding()]
param(
    [switch]$AcceptThirdPartyTerms,
    [switch]$InstallPortableJre,
    [string]$JavaHome,
    [string]$RuntimeRoot = (Join-Path $env:LOCALAPPDATA 'AstraRuntime\vm')
)

$ErrorActionPreference = 'Stop'

function Require-Condition {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) { throw $Message }
}

function Get-ContainedPath {
    param([string]$BasePath, [string]$ChildPath)
    $base = [IO.Path]::GetFullPath($BasePath).TrimEnd('\') + '\'
    $child = [IO.Path]::GetFullPath($ChildPath)
    Require-Condition ($child.StartsWith($base, [StringComparison]::OrdinalIgnoreCase)) `
        "Refusing a path outside the Astra VM runtime root: $child"
    return $child
}

function Invoke-VerifiedDownload {
    param([string]$Uri, [string]$Destination, [string]$ExpectedHash, [string]$Algorithm)
    Write-Host "Downloading $Uri"
    Invoke-WebRequest -Uri $Uri -OutFile $Destination -UseBasicParsing
    $actual = (Get-FileHash -LiteralPath $Destination -Algorithm $Algorithm).Hash.ToLowerInvariant()
    Require-Condition ($actual -eq $ExpectedHash.ToLowerInvariant()) "Checksum verification failed for $Uri"
}

function Set-AvdSetting {
    param([string]$Path, [string]$Key, [string]$Value)
    $lines = if (Test-Path -LiteralPath $Path) {
        [Collections.Generic.List[string]](Get-Content -LiteralPath $Path)
    } else {
        [Collections.Generic.List[string]]::new()
    }
    $prefix = "$Key="
    $updated = $false
    for ($index = 0; $index -lt $lines.Count; ++$index) {
        if ($lines[$index].StartsWith($prefix, [StringComparison]::Ordinal)) {
            $lines[$index] = "$Key=$Value"
            $updated = $true
        }
    }
    if (-not $updated) { $lines.Add("$Key=$Value") }
    Set-Content -LiteralPath $Path -Value $lines -Encoding ascii
}

Require-Condition $AcceptThirdPartyTerms @'
This setup downloads components directly from their publishers and accepts terms only when rerun with -AcceptThirdPartyTerms.

Google Android SDK terms: https://developer.android.com/studio/terms
Google Play system-image terms apply through sdkmanager.
Eclipse Temurin terms: https://adoptium.net/legal/

No Google Play Store, GMS, Android image, Java runtime, or user credentials are included in Astra.
'@

Require-Condition ([Environment]::Is64BitOperatingSystem) 'A 64-bit Windows installation is required.'
$cpu = Get-CimInstance Win32_Processor | Select-Object -First 1
Require-Condition $cpu.VirtualizationFirmwareEnabled 'Firmware virtualization is disabled. Enable Intel VT-x or AMD-V in BIOS/UEFI first.'

$runtimeRoot = [IO.Path]::GetFullPath($RuntimeRoot)
$sdkRoot = Get-ContainedPath -BasePath $runtimeRoot -ChildPath (Join-Path $runtimeRoot 'sdk')
$avdRoot = Get-ContainedPath -BasePath $runtimeRoot -ChildPath (Join-Path $runtimeRoot 'avd')
$androidUserHome = Get-ContainedPath -BasePath $runtimeRoot -ChildPath (Join-Path $runtimeRoot 'android-user')
$cacheRoot = Get-ContainedPath -BasePath $runtimeRoot -ChildPath (Join-Path $runtimeRoot 'cache')
New-Item -ItemType Directory -Force -Path $sdkRoot, $avdRoot, $androidUserHome, $cacheRoot | Out-Null

$driveName = [IO.Path]::GetPathRoot($runtimeRoot).TrimEnd(':', '\')
$drive = Get-PSDrive -Name $driveName
Require-Condition ($drive.Free -ge 12GB) 'At least 12 GB free disk space is required for the emulator, Play image, and AVD.'

$javaExe = $null
if ($JavaHome) {
    $candidate = Join-Path ([IO.Path]::GetFullPath($JavaHome)) 'bin\java.exe'
    Require-Condition (Test-Path -LiteralPath $candidate -PathType Leaf) "Java was not found at $candidate"
    $javaExe = $candidate
} elseif ($env:JAVA_HOME -and (Test-Path -LiteralPath (Join-Path $env:JAVA_HOME 'bin\java.exe'))) {
    $javaExe = Join-Path $env:JAVA_HOME 'bin\java.exe'
} else {
    $command = Get-Command java.exe -ErrorAction SilentlyContinue
    if ($command) { $javaExe = $command.Source }
}

if (-not $javaExe -and $InstallPortableJre) {
    $jreArchive = Join-Path $cacheRoot 'temurin-jre.zip'
    $jreUrl = 'https://api.adoptium.net/v3/binary/latest/21/ga/windows/x64/jre/hotspot/normal/eclipse'
    $expected = (Invoke-WebRequest -Uri "$jreUrl.sha256.txt" -UseBasicParsing).Content.Trim().Split()[0]
    Require-Condition ($expected -match '^[0-9a-fA-F]{64}$') 'Temurin checksum response is invalid.'
    Invoke-VerifiedDownload -Uri $jreUrl -Destination $jreArchive -ExpectedHash $expected -Algorithm SHA256
    $jreRoot = Get-ContainedPath -BasePath $runtimeRoot -ChildPath (Join-Path $runtimeRoot 'jre')
    New-Item -ItemType Directory -Force -Path $jreRoot | Out-Null
    Expand-Archive -LiteralPath $jreArchive -DestinationPath $jreRoot -Force
    $javaExe = Get-ChildItem -LiteralPath $jreRoot -Filter java.exe -Recurse | Where-Object {
        $_.FullName -match '\\bin\\java\.exe$'
    } | Select-Object -First 1 -ExpandProperty FullName
}
Require-Condition $javaExe 'Java 17+ is required. Supply -JavaHome, put java.exe on PATH, or explicitly use -InstallPortableJre.'

$env:JAVA_HOME = Split-Path -Parent (Split-Path -Parent $javaExe)
$env:ANDROID_SDK_ROOT = $sdkRoot
$env:ANDROID_HOME = $sdkRoot
$env:ANDROID_AVD_HOME = $avdRoot
$env:ANDROID_EMULATOR_HOME = $androidUserHome
$env:ANDROID_USER_HOME = $androidUserHome

$commandLineArchive = Join-Path $cacheRoot 'commandlinetools-win-14742923_latest.zip'
$commandLineUrl = 'https://dl.google.com/android/repository/commandlinetools-win-14742923_latest.zip'
# Google publishes this immutable build's 40-character checksum on the official download page.
$commandLineSha1 = '16b3f45ddb3d85ea6bbe6a1c0b47146daf0db450'
$sdkManager = Join-Path $sdkRoot 'cmdline-tools\latest\bin\sdkmanager.bat'
if (-not (Test-Path -LiteralPath $sdkManager -PathType Leaf)) {
    Invoke-VerifiedDownload -Uri $commandLineUrl -Destination $commandLineArchive -ExpectedHash $commandLineSha1 -Algorithm SHA1
    $staging = Get-ContainedPath -BasePath $runtimeRoot -ChildPath (Join-Path $cacheRoot 'cmdline-staging')
    if (Test-Path -LiteralPath $staging) { Remove-Item -LiteralPath $staging -Recurse -Force }
    New-Item -ItemType Directory -Force -Path $staging | Out-Null
    Expand-Archive -LiteralPath $commandLineArchive -DestinationPath $staging
    $source = Join-Path $staging 'cmdline-tools'
    Require-Condition (Test-Path -LiteralPath (Join-Path $source 'bin\sdkmanager.bat')) 'Official command-line tools layout changed unexpectedly.'
    $destination = Join-Path $sdkRoot 'cmdline-tools\latest'
    if (Test-Path -LiteralPath $destination) { Remove-Item -LiteralPath $destination -Recurse -Force }
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) | Out-Null
    Move-Item -LiteralPath $source -Destination $destination
}

$sdkManager = Join-Path $sdkRoot 'cmdline-tools\latest\bin\sdkmanager.bat'
$avdManager = Join-Path $sdkRoot 'cmdline-tools\latest\bin\avdmanager.bat'
Require-Condition (Test-Path -LiteralPath $sdkManager) 'sdkmanager was not installed.'
Require-Condition (Test-Path -LiteralPath $avdManager) 'avdmanager was not installed.'

$systemImage = 'system-images;android-36;google_apis_playstore;x86_64'
& $sdkManager "--sdk_root=$sdkRoot" --list | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'sdkmanager --list failed. Check Java and Internet access.' }
'y' | & $sdkManager "--sdk_root=$sdkRoot" --licenses | Out-Host
if ($LASTEXITCODE -ne 0) { throw 'Android SDK licenses were not accepted.' }
& $sdkManager "--sdk_root=$sdkRoot" 'platform-tools' 'emulator' $systemImage | Out-Host
if ($LASTEXITCODE -ne 0) { throw "Required Android 16 Google Play image was unavailable or could not be installed: $systemImage" }

$avdName = 'Astra_Play_API_36'
$avdPath = Join-Path $avdRoot "$avdName.avd"
if (-not (Test-Path -LiteralPath (Join-Path $avdPath 'config.ini'))) {
    'no' | & $avdManager create avd -n $avdName -k $systemImage -p $avdPath -f | Out-Host
    if ($LASTEXITCODE -ne 0) { throw 'avdmanager could not create the Astra Google Play device.' }
}

$config = Join-Path $avdPath 'config.ini'
Set-AvdSetting -Path $config -Key 'hw.ramSize' -Value '1536'
Set-AvdSetting -Path $config -Key 'hw.cpu.ncore' -Value '2'
Set-AvdSetting -Path $config -Key 'disk.dataPartition.size' -Value '6144M'
Set-AvdSetting -Path $config -Key 'hw.gpu.enabled' -Value 'yes'
Set-AvdSetting -Path $config -Key 'hw.gpu.mode' -Value 'auto'
Set-AvdSetting -Path $config -Key 'hw.audioInput' -Value 'no'
Set-AvdSetting -Path $config -Key 'hw.camera.back' -Value 'none'
Set-AvdSetting -Path $config -Key 'hw.camera.front' -Value 'none'

$emulator = Join-Path $sdkRoot 'emulator\emulator.exe'
& $emulator -accel-check | Out-Host
if ($LASTEXITCODE -ne 0) {
    Write-Warning 'The image is installed, but WHPX is not ready. Run enable-windows-hypervisor.ps1 as Administrator, reboot, then use astra-cli vm-status.'
}

Write-Host "Astra Android 16 Google Play VM prepared at $runtimeRoot"
Write-Host 'Use: astra-cli vm-status, then astra-cli vm-run <installed-app-id>'
