# Changelog

## v1.1 — Protected plugins and user customizations

- Redirect native Double Commander program-side `plugins`, `pixmaps`, `highlighters`, `scripts`, and `cache` folders to persistent Data locations.
- Keep official bundled content in `App\DoubleCommander\_BundledAssets` and seed/update only untouched official assets; preserve user changes and additional plugins.
- Rebuild owned directory links when portable root moves; never remove unrelated existing links.
- Recover v1.0 physical program resource directories by non-overwriting import and archival into `Data\LegacyAppFolders`.
- Add `Migrate-v1.0-Addons.cmd` and `scripts/Migrate-v1.0-Addons.ps1` for a GUI-assisted pre-upgrade backup and import of v1.0 App-side add-ons.
- Use copy fallback for drives without reparse/junction support, with explicit forced-termination limitations.
- Preserve v1.0 TraceGuard, `--config-dir`, official icon, hash verification, PortableApps Menu, manually triggered GitHub Actions, one-day artifacts and ZIP-only output.

## v1.0

- Verify official Double Commander 1.2.9 Windows x64 portable ZIP by pinned SHA-256.
- Build native Win32 launcher with original application icon and preserve app binaries.
- Use upstream `--config-dir` parameter for `Data\Config`.
- Include PortableApps metadata and conservative TraceGuard logging.
