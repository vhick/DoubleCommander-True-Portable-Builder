# Double Commander True Portable Builder v1.1

A **standalone, manually triggered GitHub Actions** builder for official Double Commander **1.2.9 Windows x64**. You do **not** need to fork the original repository or install Free Pascal. The build downloads the official portable ZIP, verifies its pinned SHA-256, compiles a native Windows launcher using the app's own icon, and creates a PortableApps-compatible ZIP.

## Important improvement in v1.1

**User-installed plugins and customizations are protected when you replace the application binaries on later upgrades.** Double Commander normally auto-installs plugins into its *program directory* in portable mode. Keeping only `Data\Config` is insufficient. Our v1.1 launcher redirects the actual application folders to `Data`, without changing Double Commander's own source code.

| Program-visible folder | Preserved portable location |
| --- | --- |
| `App\DoubleCommander\plugins` | `Data\Plugins` |
| `App\DoubleCommander\pixmaps` | `Data\CustomIcons` |
| `App\DoubleCommander\highlighters` | `Data\Highlighters` |
| `App\DoubleCommander\scripts` | `Data\Scripts` |
| `App\DoubleCommander\cache` | `Data\Cache` |
| `--config-dir` | `Data\Config` |
| TraceGuard audit | `Data\TraceGuard` |

The original program's bundled assets are stored under `App\DoubleCommander\_BundledAssets` and seeded into Data on launch. Untouched bundled files may be updated when the underlying app is upgraded; **modified or third-party files are not overwritten**. A small fingerprint in `Data\State\BundledHashes` distinguishes unmodified bundled assets from user files. Removed upstream bundled assets are never automatically deleted.

**Other customizations:** External utilities, files stored in unrelated `App` folders, absolute paths saved in user toolbars, or settings created by third-party plugins outside Double Commander's own data paths are not automatically protected. Put your own content in `Data` and configure relative paths where possible.

## Build using GitHub Desktop (existing builder repository)

1. Extract `DoubleCommander-True-Portable-Builder-v1.1.zip`.
2. Open **GitHub Desktop** and select your current `DoubleCommander-True-Portable-Builder` repository.
3. Choose **Repository → Show in Explorer**.
4. Copy the **contents** of the extracted v1.1 folder into your repository folder. Replace same-named files; ensure `.github\workflows\build.yml` stays in the repository root.
5. Return to GitHub Desktop, enter `Protect plugins and customizations - v1.1`, click **Commit to main**, then **Push origin**.
6. Open GitHub → **Actions → Build Double Commander True Portable → Run workflow**.
7. On success, download `DoubleCommanderPortable-v1.2.9-win64` under **Artifacts**, then extract the actual `DoubleCommanderPortable-v1.2.9-win64.zip` inside it.

A new repository can also be created by **File → New repository**, copying the builder files, then **Commit to main → Publish repository**.

## CRITICAL: Upgrading an existing v1.0 portable installation

**Do not delete the old `App` folder until you have migrated the existing add-ons.** Your v1.0 application may contain plugins under `App\DoubleCommander\plugins`, not under `Data`.

1. Fully exit Double Commander (including any background/tray instance).
2. **Back up the entire existing `DoubleCommanderPortable` folder** to another location.
3. In the *extracted v1.1 BUILDER kit* (not in the downloaded portable application), **double-click `Migrate-v1.0-Addons.cmd`**. Select your **current** `DoubleCommanderPortable` folder when asked. This helper copies relevant old App files to `Data`, writes a backup under `Data\UpgradeBackup-v1.0-<date>`, and **never deletes your old files or replaces existing Data files**.
4. Download your successfully built v1.1 portable application. In your existing portable folder, **replace `App`, `Other`, and `DoubleCommanderPortable.exe`** with the new build. **Preserve the ENTIRE original `Data` directory.** Do not extract the new ZIP over `Data` if Windows offers to replace files.
5. Open using the **root `DoubleCommanderPortable.exe`**, not `App\DoubleCommander\doublecmd.exe`.
6. Test the Plugins settings and one installed plugin, then exit and inspect `Data\Plugins` and `Data\TraceGuard\TraceGuard.log`.

The launcher also detects an old *real* `App\DoubleCommander\plugins` folder left in place, copies previously unprotected content into Data and archives the former App folder under `Data\LegacyAppFolders` before linking the path. **This is recovery protection, not a replacement for step 2 or the pre-upgrade migration helper.**

## Resulting application folder

```text
DoubleCommanderPortable/
  DoubleCommanderPortable.exe
  App/
    DoubleCommander/
      doublecmd.exe
      _BundledAssets/   (official bundled plugins/icons/etc.)
      plugins/          (link to Data\Plugins, created on launch)
      pixmaps/          (link to Data\CustomIcons, created on launch)
      highlighters/     (link to Data\Highlighters, created on launch)
      scripts/          (link to Data\Scripts, created on launch)
      cache/            (link to Data\Cache, created on launch)
    AppInfo/
      appinfo.ini
      appicon.ico
      appicon_16.png
      appicon_32.png
  Data/
    Config/
    Plugins/
    CustomIcons/
    Highlighters/
    Scripts/
    Cache/
    State/
    LegacyAppFolders/
    TraceGuard/
  Other/Help/README.txt
```

The folder links shown under `App` are created on the first launcher run. **On NTFS**, a junction or symbolic link normally allows real-time storage directly in `Data`. Moving to another NTFS drive recreates app-managed links if required. The launcher refuses to remove unfamiliar links. **On exFAT/FAT/network drives**, if links cannot be created, the launcher uses a best-effort copy-in/copy-back fallback. On these filesystems, changes made while running may be lost after a crash or forced shutdown; use NTFS for the strongest behavior. The launcher does not silently remove user resources.

## Test after upgrade

1. Launch Double Commander from `DoubleCommanderPortable.exe`.
2. In **Configuration → Options → Plugins**, check that previously installed plugins still load. The program-relative `plugins` directory should lead into `Data\Plugins` on supported drives.
3. Install a **new** small plugin through the normal plugin interface. Exit and inspect `Data\Plugins`; the plugin should now be there.
4. Set a custom icon/script, restart, and confirm the customization persists.
5. Move the entire portable folder to a new directory/drive and test again; check any saved **absolute plugin paths**.
6. Check `Data\TraceGuard\TraceGuard.log` for redirection, migration, and new app-named host traces.

## Safety, compatibility, and limitations

- No Windows file associations, automatic startup entries, Explorer replacements, services or permanent context-menu registrations are installed by the builder or launcher.
- The upstream app and plugins are not modified, and file operations made within Double Commander remain real (copy/move/delete).
- TraceGuard reports application-named file/registry changes; only newly created empty app-specific host directories are candidates for cleanup. Shared Windows caches/registry are not deleted.
- Plugins may create other host files or registry entries. TraceGuard does not guarantee perfect isolation.
- **No multi-instance guarantee**: if an existing Double Commander process intercepts the launcher request, it may use another profile. Exit all instances before testing.
- App resource-link setup needs a writable extracted folder. Read-only Program Files or restricted volumes are not supported.
- **Built and runtime-tested?** The kit's files, layout and ZIP are locally checked. Windows GitHub Actions compilation, junction creation, plugin installation and upgrade testing still need a real Windows run.

## Sources

- Original project and published releases: https://github.com/doublecmd/doublecmd/releases
- Double Commander configuration and plugin directories: https://doublecmd.github.io/doc/en/configuration.html
- Automatic plugin installation details: https://doublecmd.github.io/doc/en/cmds.html
- Application path variables: https://doublecmd.github.io/doc/en/variables.html

Double Commander has its own upstream license. Preserve its license and notices when redistributing the official application. The builder's extra launcher and support scripts are MIT-licensed.
