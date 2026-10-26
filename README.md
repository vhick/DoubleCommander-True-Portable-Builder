# Double Commander True Portable Builder v1.0

A standalone manually-run GitHub Actions builder for Double Commander v1.2.9 **Windows x64**. No fork and no Lazarus compilation required. It uses the **official ZIP** from [doublecmd/doublecmd](https://github.com/doublecmd/doublecmd/releases/tag/v1.2.9), verifies the pinned SHA-256, and compiles only our native Windows launcher.

## Build using GitHub Desktop

1. Extract this builder ZIP.
2. Open **GitHub Desktop → File → New repository**, name it `DoubleCommander-True-Portable-Builder`, leave `Initialize with README` unchecked, and create it.
3. Choose **Repository → Show in Explorer**.
4. Copy the **contents** of this extracted builder folder (including `.github`) into the repository folder.
5. Back in GitHub Desktop: **Commit to main → Publish repository**.
6. Open the new repository on GitHub → **Actions → Build Double Commander True Portable → Run workflow**.
7. Once it succeeds, open the run and download artifact `DoubleCommanderPortable-v1.2.9-win64`.
8. Extract the artifact to obtain **`DoubleCommanderPortable-v1.2.9-win64.zip`**. Extract that ZIP into your PortableApps directory.
9. Start the application via `DoubleCommanderPortable.exe` (not directly via `App\DoubleCommander\doublecmd.exe`).

## Resulting structure

```text
DoubleCommanderPortable/
  DoubleCommanderPortable.exe
  App/
    DoubleCommander/
      doublecmd.exe
      ... official distribution files, plugins and settings marker ...
    AppInfo/
      appinfo.ini
      appicon.ico
      appicon_16.png
      appicon_32.png
  Data/
    Config/
    TraceGuard/
  Other/
    Help/
      README.txt
```

## Why the launcher redirects configuration

Double Commander officially supports `--config-dir=C:\path\to\settings`, so the launcher passes the absolute current `Data\Config` location, regardless of where the portable folder is moved. This keeps normal Double Commander configuration in `Data`, while its program files can be replaced on upgrades. The official `settings/doublecmd.inf` marker is also preserved as a fallback if you start the underlying program directly; doing so stores settings under `App`, **not** under the protected `Data` folder, so use the launcher.

## Safety and limitations

- The launcher does **not** register file associations, shell extensions, default file manager, services, startup tasks or permanent shortcuts.
- TraceGuard snapshots only the exact app-named `%APPDATA%\doublecmd`, `%LOCALAPPDATA%\doublecmd`, and `%USERPROFILE%\.doublecmd` locations, plus checks for select app-named HKCU registry keys. It logs newly created files but never removes them. It removes **only new empty app-specific directories** when the entire root did not already exist, preserving pre-existing host data. Shared Windows files and registry are never deleted.
- Third-party plugins and optional Explorer integrations may leave their own Windows traces. Windows recent-file history, clipboard contents and Explorer records are **not** erased.
- Double Commander's normal file operations are real. The launcher never blocks copying, deleting or moving user files.
- If you already run another Double Commander instance, its single-instance behavior might reuse that instance. Close existing instances while testing, so you can verify this profile accurately.
- Tabs, tool buttons, plugins and other settings that store **absolute paths** may need updating when moving to another drive. Automatic path rewriting would risk corrupting user configuration.
- No claim of zero host traces until a successful Windows runtime audit.

## Upgrade without losing settings

The builder pins version **1.2.9** and its expected SHA-256. Update the version **and checksum** in `scripts/build.ps1` if a newer release becomes available (also update the artifact and display version in `.github/workflows/build.yml` / `appinfo.ini`). Keep the existing `Data` folder; replace the launcher, `App`, and `Other` with the new build. Don't overwrite `Data`.

## Verify on Windows

1. Start `DoubleCommanderPortable.exe`, configure tabs, favorites, toolbar and hotkeys.
2. Fully close Double Commander, then open it again.
3. Verify your preferences have persisted under `Data\Config`.
4. Move the whole `DoubleCommanderPortable` folder to another drive; launch again and confirm configuration survives.
5. Read `Data\TraceGuard\TraceGuard.log`. Report any unexpected new app-specific host traces, especially files, and share the log for further adjustments.

## Provenance

- Source: https://github.com/doublecmd/doublecmd
- Official portable ZIP: https://github.com/doublecmd/doublecmd/releases/download/v1.2.9/doublecmd-1.2.9.x86_64-win64.zip
- Checksum reference: https://sourceforge.net/projects/doublecmd/files/Double%20Commander/v1.2.9/doublecmd-1.2.9.x86_64-win64.zip/download
- Official portability guide: https://github.com/doublecmd/doublecmd/wiki/Testing-different-version

Double Commander licensing remains governed by its upstream distribution. The launcher and builder glue are MIT licensed; retain upstream licensing and notices when distributing.
