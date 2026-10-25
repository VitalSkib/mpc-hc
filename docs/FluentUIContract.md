# Native Fluent UI behavior contract

Keep these requirements when changing source labels, metadata, composition or mouse input.

- The channel heading in a source-bound programme playlist has no row number. The first programme is 1. Channel identity does not depend on successfully parsing programme dates. Regular file playlists keep ordinary numbering.
- The titlebar shows the programme title only. Its tooltip may contain time and channel. A label-only schedule must keep this separation when structured EPG is empty.
- The visible titlebar owns mouse input. Background click must not toggle playback. Drag delegates to the main frame's native HTCAPTION movement loop, preserving Windows move/snap behavior. Background double click uses the existing maximize/restore command. Window buttons retain their actions. Fullscreen background must not move the fullscreen window.
- Title tooltip input belongs to the header HWND. Its rectangle is local to that HWND. Verify tooltip after resize/fullscreen transitions.
- Other video clicks and OSD commands remain native.
- Playlist content, Acrylic and shadow use one animated presentation. Verify open/close both before and after wheel/scroll-thumb movement. Retain creation-time host rendering mode.
- Statistics/Status retain native painters and data flow; verify labels and values after startup and fullscreen changes.

Automated coverage: NativeProgrammeGuideTests (metadata presence/absence, captions and channel identity) and NativePlaylistScrollTests (native scrolling and host recreation).
Manual review required: header drag, single/double click, window buttons, title tooltip, ordinary video click, and playlist scroll/hide artifacts. Compilation does not constitute this UI validation.

Resize routing: a chrome child is not a frame. A stock view resize hit must return HTTRANSPARENT from header and adopted footer descendants, leaving the original CChildView SC_SIZE handler authoritative. Caption actions still use frame nonclient messages. Test existing and dynamically created descendants, normal widget input and restoration cleanup. Standalone frame hit-test success alone is not proof of working resize through overlays.


## Accepted rollback after resize experiments, 2026-10-25

At the user's request, use application sources from 996f084, the checkpoint
where resize routing through visible chrome was confirmed working, before the
subsequent smoothness experiments. This is not a claim of smooth live resize.
Preserve the approved styling, playlist, programme guide, title/tooltips and
native input changes already present at that checkpoint.

Remove all later resize experiment code: composition-surface reuse changes,
frame/view notification deferrals/restorations, hosted-toolbar geometry changes,
extra pending-paint handling and buffered resize stage timing. Remove the unused
NativeResizeTiming.h. The application source tree must match 996f084 exactly.

The sole build-policy difference from that checkpoint is that heavy
MPC_EXPERIMENTAL_GEOMETRY_DIAGNOSTICS remains disabled in the normal styled
build. Retain the three normal presentation flags. Do not restore per-message
UI logging or per-present renderer logging: disabling that instrumentation
produced the user-confirmed responsiveness improvement.

Keep current portable INI, selected renderer and routing. Do not copy an old INI.
Back up the installed EXE; do not launch the player or automate desktop tests.
No further resize experiments without a new user request.
