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

During native frame RecalcLayout, already-mounted chrome and its video geometry
are published at the existing synchronous final update, not again from child
WM_SIZE inside DeferWindowPos. Direct view resizing and initial chrome mounting
keep their original handlers. No resize timer, frame-rate cap or renderer change
is introduced. Re-test opening media, docking, fullscreen return, text bars and
live corner dragging before accepting the candidate.

## One-variable resize isolation

`[Settings] FluentHeaderAcrylic=1` (default) retains the approved header material.
Set it to `0` and restart to omit only the header Acrylic controller/composition
target and use its existing flat-alpha fallback. Header input, title/tooltips,
chrome animation, footer and playlist remain selected as before. Tint=0 is not
an off switch. Restore the baseline before testing a different effect. Record
the tested commit and setting; this diagnostic option is not a permanent style
decision. HWND style selection follows the option at creation time.

`[Settings] FluentFooterBackdrop=1` is the normal shared shade beneath the
hosted footer. Set only this option to `0` to omit that decoration window and
bitmap while keeping control layout/input, header material and chrome motion.
Restart for a clean comparison. This does not disable the toolbar alpha artwork
or the playlist material. Restore the previous test's options before using it.

## Uninstrumented control build

Do not enable MPC_EXPERIMENTAL_GEOMETRY_DIAGNOSTICS for normal performance
comparisons. It instruments the renderer Present path and enumerates every
toolbar item before/after many native mutations, with synchronous log writes.
Keep that opt-in for focused diagnostics. The normal styled build keeps the
same native-controls/material flags without geometry/UI tracing. Compare with
all effects restored first. SWP_NOCOPYBITS on the video view remains a separate
historical intervention; do not conflate it with tracing in the same test.

## Native video pixel retention

Do not force SWP_NOCOPYBITS on CChildView during normal resize/move. Its native
valid-pixel policy and presenter updates remain responsible for video pixels.
Keep explicit discard flags on transparent chrome hosts separate: those flags
addressed copied alpha bands, and are not a general policy for the video HWND.
This comparison follows the uninstrumented control build, with effects enabled.
Verify live resize as well as old-image strips under the seekbar, animation,
fullscreen return and first media sizing before accepting the result.

## Opt-in live resize timing

FluentResizeTiming=1 records UI-stage QPC durations into a fixed 4096-entry
memory buffer only between native EnterSizeMove and ExitSizeMove. No hot-path
I/O, formatting, heap allocations or additional window/toolbar queries are used.
After the drag ends, MPC-HC-resize-profile.csv next to the executable is replaced
with the last session. Durations are inclusive and nested; they cannot be summed
as independent costs and do not measure GPU presentation or compositor latency.
This diagnostic changes no layout, renderer or material behavior and is off by
default. Keep the old geometry diagnostics disabled.
