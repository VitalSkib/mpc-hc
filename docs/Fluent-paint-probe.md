# Fluent toolbar paint probe

This branch starts with a deliberately small experiment, not a completed Fluent UI.
The `MPC_EXPERIMENTAL_NO_TOOLBAR_FILL` compiler define suppresses the toolbar's
client background, separator overpainting, and non-client fill. It preserves the
native button image lists, commands, hit testing, tooltips, and button-state drawing.
Without the define, the upstream paint path remains unchanged.

The experiment does not yet modify the seekbar, volume slider, video layout,
top-right close button, or hover title. Skipping a GDI fill does not establish
that the video renderer will compose behind that window. Inspect the actual build
for opaque parent backgrounds, unpainted regions, and stale pixels during resize,
pause, cover/uncover, Minimal/Compact mode, and hover show/hide.

## Build and isolate

The GitHub Actions job produces an x64 **Lite** test build: internal codecs are not
included. See `Compilation.md` for the full build prerequisites. Tests of local
files may require separately installed external filters. This is not a release.

Extract the artifact to a separate writable folder, never over the installed
player. Before launching, create `mpc-hc64.ini` alongside `mpc-hc64.exe` with:

```ini
[Settings]
; Portable experiment; do not copy the installed player's settings.
```

Launch the extracted executable with `/new` so the request does not get forwarded
to the installed player's process. Keep the test INI alongside the executable to
avoid using the installed player's registry settings. Confirm portable settings
before changing options. Do not use
Firefox's existing `mpc://` registration for this probe. The installed player,
MpcHover helper, and browser userscripts are not part of this change.

## Expected next step

First verify that the build compiles and the icons remain visible when backgrounds
are omitted. Only then choose the required composition approach for translucent
panels, and add the native hover title and top-right close action.
