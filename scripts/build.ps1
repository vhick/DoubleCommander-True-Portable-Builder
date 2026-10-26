#requires -Version 5.1
# Build a PortableApps-friendly package from the OFFICIAL portable ZIP, with a native launcher.
# Pinning checksum is intentional: update the version and hash together when upgrading.
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$Version = '1.2.9'
$Sha256 = 'e8472c0105dbde55db4ffbdbd190eab65e9da0375596454995fae4a1fc128dee'
$Repository = Split-Path -Parent $PSScriptRoot
$Work = Join-Path $Repository '_work'
$Dist = Join-Path $Repository 'dist'
$Portable = Join-Path $Work 'DoubleCommanderPortable'
$Program = Join-Path $Portable 'App\DoubleCommander'
$AppInfo = Join-Path $Portable 'App\AppInfo'
$Archive = Join-Path $Work "doublecmd-$Version.x86_64-win64.zip"
$DownloadUrl = "https://github.com/doublecmd/doublecmd/releases/download/v$Version/doublecmd-$Version.x86_64-win64.zip"

if (Test-Path $Work) { Remove-Item -LiteralPath $Work -Recurse -Force }
New-Item -ItemType Directory -Force -Path $Work, $Dist, $Portable, $Program, $AppInfo, (Join-Path $Portable 'Data\Config'), (Join-Path $Portable 'Data\TraceGuard'), (Join-Path $Portable 'Other\Help') | Out-Null
# Keep the initially empty Data subdirectories in the generated ZIP.
Set-Content -LiteralPath (Join-Path $Portable 'Data\Config\.keep') -Value '' -NoNewline -Encoding Ascii
Set-Content -LiteralPath (Join-Path $Portable 'Data\TraceGuard\.keep') -Value '' -NoNewline -Encoding Ascii
Write-Host "Downloading official Double Commander $Version portable release"
Invoke-WebRequest -Uri $DownloadUrl -OutFile $Archive -MaximumRedirection 10
$Actual = (Get-FileHash -LiteralPath $Archive -Algorithm SHA256).Hash.ToLowerInvariant()
if ($Actual -ne $Sha256) { throw "Official archive SHA-256 mismatch: got $Actual, expected $Sha256. Stop; do not bypass verification." }
Write-Host "Official release SHA-256 verified: $Actual"

$Unpack = Join-Path $Work 'unpacked'
Expand-Archive -LiteralPath $Archive -DestinationPath $Unpack -Force
$Executables = @(Get-ChildItem -LiteralPath $Unpack -Recurse -Filter 'doublecmd.exe' -File)
if ($Executables.Count -ne 1) { throw "Expected exactly one doublecmd.exe in archive, found $($Executables.Count)." }
$Source = $Executables[0].Directory.FullName
Write-Host "Official application root: $Source"
Copy-Item -Path (Join-Path $Source '*') -Destination $Program -Recurse -Force
$DcExe = Join-Path $Program 'doublecmd.exe'
if (!(Test-Path -LiteralPath $DcExe)) { throw 'Official doublecmd.exe missing after packaging.' }
# Keep the official settings/doublecmd.inf, but move the five application-controlled
# mutable resource folders to a read-only baseline alongside the executable. The launcher
# merges this baseline into Data without overwriting user additions, then links the actual
# program-relative folder paths back to Data. Only the generated App can be replaced on upgrade.
$Bundled = Join-Path $Program '_BundledAssets'
New-Item -ItemType Directory -Force -Path $Bundled | Out-Null
$UserFolders = @{
    'plugins'      = 'Plugins'
    'pixmaps'      = 'CustomIcons'
    'highlighters' = 'Highlighters'
    'scripts'      = 'Scripts'
    'cache'        = 'Cache'
}
foreach ($Name in $UserFolders.Keys) {
    $Original = Join-Path $Program $Name
    if (Test-Path -LiteralPath $Original) {
        if (!(Test-Path -LiteralPath $Original -PathType Container)) {
            throw "Expected a directory for managed resource '$Name': $Original"
        }
        Move-Item -LiteralPath $Original -Destination (Join-Path $Bundled $Name)
    }
    $DataFolder = Join-Path $Portable ("Data\" + $UserFolders[$Name])
    New-Item -ItemType Directory -Force -Path $DataFolder | Out-Null
    Set-Content -LiteralPath (Join-Path $DataFolder '.keep') -Value '' -NoNewline -Encoding ASCII
}
New-Item -ItemType Directory -Force -Path (Join-Path $Portable 'Data\State'), (Join-Path $Portable 'Data\LegacyAppFolders') | Out-Null
# The original settings/doublecmd.inf stays in App as a fallback. The launcher overrides
# settings with --config-dir so ordinary configuration is always in Data\Config.

# Extract the actual, installed application's icon, not a generic replacement.
Add-Type -AssemblyName System.Drawing
$Icon = [System.Drawing.Icon]::ExtractAssociatedIcon($DcExe)
if ($null -eq $Icon) { throw 'Could not extract the original Double Commander executable icon.' }
$IconPath = Join-Path $AppInfo 'appicon.ico'
$Stream = [IO.File]::Open($IconPath, [IO.FileMode]::Create)
try { $Icon.Save($Stream) } finally { $Stream.Dispose() }
foreach ($size in @(16, 32)) {
    $Image = New-Object System.Drawing.Bitmap($size, $size)
    $Graphics = [System.Drawing.Graphics]::FromImage($Image)
    try {
        $Graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $Graphics.DrawImage($Icon.ToBitmap(), 0, 0, $size, $size)
        $Image.Save((Join-Path $AppInfo "appicon_$size.png"), [System.Drawing.Imaging.ImageFormat]::Png)
    } finally { $Graphics.Dispose(); $Image.Dispose() }
}
$Icon.Dispose()

$Ini = @"
[Format]
Type=PortableApps.comFormat
Version=3.8

[Details]
Name=Double Commander Portable
AppID=DoubleCommanderPortable
Publisher=Double Commander Team
Homepage=https://doublecmd.sourceforge.io/
Category=Utilities
Description=Two-panel open-source file manager
Language=Multilingual

[License]
Shareable=true
OpenSource=true
Freeware=true
CommercialUse=true
EULAVersion=1

[Version]
PackageVersion=1.2.9.0
DisplayVersion=1.2.9

[Control]
Icons=1
Start=DoubleCommanderPortable.exe
"@
Set-Content -LiteralPath (Join-Path $AppInfo 'appinfo.ini') -Value $Ini -Encoding Ascii
@'
Use DoubleCommanderPortable.exe at the package root.
Data\Config stores settings; Data\Plugins stores new plugins.
Data\CustomIcons, Data\Highlighters, Data\Scripts and Data\Cache preserve additions.
The root launcher redirects actual plugin folders to Data. DO NOT run doublecmd.exe directly.
Before upgrading from v1.0, run the migration helper and back up the current portable folder.
On upgrades, replace App and launcher but preserve the ENTIRE Data folder.
This package does not install Explorer integrations, file associations, shortcuts or services.
WARNING: Any file-operation changes you make inside Double Commander are real and permanent.
'@ | Set-Content -LiteralPath (Join-Path $Portable 'Other\Help\README.txt') -Encoding UTF8

# Compile the launcher in the MSVC developer command prompt supplied by GitHub's Windows runner.
$Vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (!(Test-Path -LiteralPath $Vswhere)) { throw 'vswhere.exe not found on Windows runner.' }
$VisualStudio = (& $Vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1)
if ([string]::IsNullOrWhiteSpace($VisualStudio)) { throw 'MSVC build tools not found.' }
$VcVars = Join-Path $VisualStudio 'VC\Auxiliary\Build\vcvars64.bat'
if (!(Test-Path -LiteralPath $VcVars)) { throw 'VC toolchain setup script not found.' }
$BuildTmp = Join-Path $Work 'native'
New-Item -ItemType Directory -Force -Path $BuildTmp | Out-Null
Copy-Item -LiteralPath $IconPath -Destination (Join-Path $BuildTmp 'appicon.ico')
Set-Content -LiteralPath (Join-Path $BuildTmp 'launcher.rc') -Value '1 ICON "appicon.ico"' -Encoding Ascii
$LauncherCpp = Join-Path $Repository 'launcher\DoubleCommanderPortable.cpp'
$RootLauncher = Join-Path $Portable 'DoubleCommanderPortable.exe'
$Batch = Join-Path $BuildTmp 'compile.cmd'
$Lines = @(
    '@echo off',
    "call `"$VcVars`" >nul || exit /b 1",
    "cd /d `"$BuildTmp`" || exit /b 1",
    'rc.exe /nologo /fo "launcher.res" "launcher.rc" || exit /b 1',
    "cl.exe /nologo /std:c++17 /EHsc /O2 /W4 /DUNICODE /D_UNICODE /Fe:`"$RootLauncher`" `"$LauncherCpp`" `"$BuildTmp\launcher.res`" /link /SUBSYSTEM:WINDOWS Kernel32.lib User32.lib Shell32.lib Advapi32.lib || exit /b 1"
)
Set-Content -LiteralPath $Batch -Value $Lines -Encoding ASCII
& cmd.exe /d /c "`"$Batch`""
if ($LASTEXITCODE -ne 0) { throw "Native launcher compilation failed with exit code $LASTEXITCODE." }

# Assert contents prior to packaging. Explicitly verify that the Data folder exists.
$Required = @(
  $RootLauncher,
  $DcExe,
  (Join-Path $AppInfo 'appinfo.ini'),
  (Join-Path $AppInfo 'appicon.ico'),
  (Join-Path $AppInfo 'appicon_16.png'),
  (Join-Path $AppInfo 'appicon_32.png'),
  (Join-Path $Portable 'Data\Config'),
  (Join-Path $Portable 'Data\TraceGuard'),
  (Join-Path $Portable 'Data\Plugins'),
  (Join-Path $Portable 'Data\CustomIcons'),
  (Join-Path $Portable 'Data\Highlighters'),
  (Join-Path $Portable 'Data\Scripts'),
  (Join-Path $Program '_BundledAssets'),
  (Join-Path $Portable 'Other\Help\README.txt')
)
foreach ($Item in $Required) { if (!(Test-Path -LiteralPath $Item)) { throw "Package missing expected item: $Item" } }
foreach ($Item in @($RootLauncher, $DcExe, $IconPath)) { if ((Get-Item -LiteralPath $Item).Length -le 0) { throw "Zero-byte executable/resource: $Item" } }
$Output = Join-Path $Dist "DoubleCommanderPortable-v$Version-win64.zip"
if (Test-Path $Output) { Remove-Item -LiteralPath $Output -Force }
Compress-Archive -LiteralPath $Portable -DestinationPath $Output -CompressionLevel Optimal
if (!(Test-Path $Output) -or (Get-Item $Output).Length -le 10000) { throw 'Final ZIP was not created correctly.' }
Write-Host "SUCCESS: $Output"
