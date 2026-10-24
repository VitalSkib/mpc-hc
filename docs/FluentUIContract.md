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

## Continuous window resizing

Keep the native SC_SIZE path through the video view. Chrome artwork keeps its
composition surface, surface brush and clip geometry across size changes; resize
the existing surface through ICompositionDrawingSurfaceInterop::Resize. After a
complete synchronous alpha publication, consume its pending paint region rather
than upload the same layout again. Do not throttle, postpone or replace native
video layout. Verify continuous dragging with visible and hidden chrome, corners,
fullscreen return, caption input, playlist animation and native text bars.
