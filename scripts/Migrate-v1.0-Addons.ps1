#requires -Version 5.1
# Read-only source migration: NOTHING from the existing portable app is deleted.
# Launch via the accompanying double-clickable .cmd file before replacing App.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
$dialog = New-Object System.Windows.Forms.FolderBrowserDialog
$dialog.Description = 'Select your CURRENT DoubleCommanderPortable folder (the one with Data and App inside).'
$dialog.ShowNewFolderButton = $false
if ($dialog.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK) { return }
$root = $dialog.SelectedPath
$exe = Join-Path $root 'App\DoubleCommander\doublecmd.exe'
if (!(Test-Path -LiteralPath $exe -PathType Leaf)) {
    [System.Windows.Forms.MessageBox]::Show('This is not a DoubleCommanderPortable folder. No changes made.','Migration', 'OK', 'Warning') | Out-Null
    return
}
# A predictable folder name is helpful for backup; use a suffix if run twice.
$when = Get-Date -Format 'yyyyMMdd-HHmmss'
$backup = Join-Path $root ('Data\UpgradeBackup-v1.0-' + $when)
$data = Join-Path $root 'Data'
New-Item -ItemType Directory -Force -Path $data | Out-Null
$folders = [ordered]@{ plugins='Plugins'; pixmaps='CustomIcons'; highlighters='Highlighters'; scripts='Scripts'; cache='Cache' }
$count = 0
$conflicts = 0
foreach ($srcName in $folders.Keys) {
    $source = Join-Path (Join-Path $root 'App\DoubleCommander') $srcName
    if (!(Test-Path -LiteralPath $source -PathType Container)) { continue }
    if (((Get-Item -LiteralPath $source).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { continue }
    $dest = Join-Path $data $folders[$srcName]
    $archive = Join-Path $backup $srcName
    $prefix = $source.TrimEnd('\') + '\'
    foreach ($file in Get-ChildItem -LiteralPath $source -File -Recurse -Force) {
        if (($file.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { continue }
        $rel = $file.FullName.Substring($prefix.Length)
        $archived = Join-Path $archive $rel
        New-Item -ItemType Directory -Path (Split-Path $archived -Parent) -Force | Out-Null
        Copy-Item -LiteralPath $file.FullName -Destination $archived -ErrorAction Stop
        $target = Join-Path $dest $rel
        New-Item -ItemType Directory -Path (Split-Path $target -Parent) -Force | Out-Null
        if (Test-Path -LiteralPath $target) { $conflicts++ }
        else { Copy-Item -LiteralPath $file.FullName -Destination $target -ErrorAction Stop; $count++ }
    }
}
[System.Windows.Forms.MessageBox]::Show(
    "Migration backup saved to:`n$backup`n`nNew files copied to Data: $count`nExisting Data files preserved: $conflicts`n`nNow replace the App folder using the new v1.1 portable build, but KEEP the entire Data folder.",
    'Double Commander v1.1 migration complete','OK','Information') | Out-Null
