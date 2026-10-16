# Native alpha overlay probe

This opt-in x64 Lite build defines `MPC_EXPERIMENTAL_ALPHA_OVERLAY`.
The earlier toolbar-fill experiment is disabled; upstream toolbar drawing is restored.

Hover over the main video view to display:
- the current native player window title at top left;
- a near-white close button at top right (standard WM_CLOSE);
- a bottom test panel with 64/255 black alpha and opaque text.

The overlay uses a native layered child window and UpdateLayeredWindow with
premultiplied per-pixel alpha, following the mechanism already used in this codebase.
It does not add WinUI, a helper process, polling, or timers. Rendering happens on
mouse entry, title/size changes observed during hover, or close-button hover changes.
Other mouse input passes through; normal toolbar, seekbar and volume remain unchanged.
This is a compositing probe, not a completed Fluent controls implementation.

## Separate test only

Extract the artifact to a separate writable folder. Close the regular player.
Create an adjacent mpc-hc64.ini containing `[Settings]` before launching mpc-hc64.exe.
Do not replace the installed player or change the Firefox protocol handler.
This is a Lite build: internal codecs are not included; use an available external
DirectShow decoder. Test real video in windowed Minimal/Compact mode first.
Exclusive fullscreen/alternate fullscreen video windows are outside this first probe.

Check whether moving video remains visible under the lower panel, while text and
close stay opaque. Check hover exit, resize, minimize/restore, fullscreen transitions,
normal playback clicks, and close cancel/confirmation behavior.
Build success alone does not establish visual or renderer compatibility.
