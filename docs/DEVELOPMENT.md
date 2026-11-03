# Development

## Build and test

Requirements: Windows x64, Visual Studio 2022 C++ Build Tools and a Windows SDK.
Close the running app before rebuilding its executable, then run `build.cmd`.

The build produces `build/S-XMTray.exe`, `build/S-XMTray-cli.exe` and four test executables.
Protocol, feature, hotkey validation and native UI tests run automatically.
No build packages are downloaded; the runtime is statically linked.

`build/hotkey-tests.exe --windows` also checks Windows registration and conflicts.
It temporarily uses free Ctrl+Alt+Shift+F13…F24 combinations and releases them on exit.
It needs an environment that permits global hotkey registration.

`build/ui-tests.exe <existing-output-directory>` renders real dialogs in both themes
with synthetic device state. It makes no Bluetooth connection or settings writes.
It checks layout, hover repainting, repeated frames, GDI resource cleanup and input behaviour.

## Package

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/package.ps1
```

The portable ZIP and SHA-256 checksum are written to `out/`. Only the GUI executable,
documentation, license, third-party notices and README images are packaged.
GitHub Actions builds/tests/packages pushes and pull requests. Releases are published separately.

## Diagnostics

Close the tray app before using the CLI so only one controller owns the connection.

```bat
build\S-XMTray-cli.exe status
build\S-XMTray-cli.exe inspect
```

These commands read state and capabilities. `anc`, `ambient` and `off` change the
listening mode and verify the result. `verify-features` is an explicit hardware write
test: it changes Speak-to-Chat, sidetone, a preset and custom Clear Bass, then restores
the original values. Do not run it as a routine diagnostic or in CI.

The GUI accepts `--panel`, `--settings`, `--hotkeys`, `--equalizer`, `--about` and `--menu`.
The last option opens the legacy Windows menu.

## Compatibility

WF-1000XM5 only. Hardware readbacks were verified on firmware 6.1.0. Unknown optional
capabilities are disabled. Bluetooth addresses stay in memory; the app does not
persist protocol traffic or send telemetry.

The `QuietTray` settings directory, startup registry value, window class and singleton
identifier are retained from development builds so existing preferences and hotkeys
continue to work. Public executable names and UI use S-XMTray.

Icons can be regenerated with `python tools/make_icons.py`. The Android package,
private development folders and design references are excluded from Git.
