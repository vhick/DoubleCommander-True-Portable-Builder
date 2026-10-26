# Changelog

## v1.0
- Verify the official Double Commander v1.2.9 Windows x64 portable ZIP by pinned SHA-256.
- Build a native Win32 launcher with official icon and preserve app binaries unmodified.
- Use the upstream-documented --config-dir parameter to store settings in Data\Config.
- Package App/AppInfo PortableApps metadata plus the original icon at PNG/ICO sizes.
- Add conservative TraceGuard logs for app-specific host paths and registry key creation.
- Keep pre-existing data; never delete host files or user-managed content.
- Manual GitHub Actions workflow, ZIP-only output, one-day artifact retention.
