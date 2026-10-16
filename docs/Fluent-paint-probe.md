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

## Native controls bridge

The lower test label is replaced with Play/Pause, timeline, and the native time text.
Button enabled state is read from the real toolbar; clicks use ID_PLAY_PLAYPAUSE.
Seek input is translated into the native seekbar channel and dispatched to its
existing mouse handlers. The native seekbar retains capture, pause/resume while
dragging, seek throttling, and actual seek commands. Input routing also handles
release outside the overlay and excludes unrelated menus and dialogs.
The native position updates and command-update cycle refresh the overlay; no
additional timer is created. Bottom availability follows the existing toolbar/seekbar selection,
not the temporary visibility of their auto-hidden HWNDs. Original controls are deliberately retained for comparison at this stage;
volume, menus and other buttons have not been replaced.

Runtime acceptance still required: play/pause via both UI and keyboard; seek by
click/drag including release outside the view; disabled seek for unseekable live
streams; hover reveal/hide; no interference with native toolbar or context menus.

## Hover reconciliation

Hover checks now verify the window actually under the cursor, not only its
coordinates. Player-wide mouse move/leave events and the existing position/UI
update cycle reconcile visibility, including renderer HWNDs on another thread.
No extra polling timer or global mouse hook is added. Missing renderer leave
events can therefore be corrected on the next existing player update.
The overlay is re-raised if a newly created renderer child is above it.
Releasing a pressed control outside the view still completes capture cleanup.
