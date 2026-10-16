# Translucent native controls experiment

Opt-in x64 Lite build: MPC_EXPERIMENTAL_ALPHA_OVERLAY. The older no-fill flag
is disabled. No installed player or Firefox handler is changed by this build.

## Interface

- Thin translucent header, flat title and top-right close button. Hover adds a
  neutral square highlight and makes the cross white. No shadows or bevels.
- Lower controls mirror the selected native toolbar buttons, order, image list,
  enabled/checked states and dynamic Play/Pause state. Native toolbar-layout and
  image-theme options remain the source of truth; no separate button allowlist.
- Timeline selection follows the native seekbar selection. Volume uses the native
  volume control. Native time text is used, with status-bar time as fallback.
- Small windows use 24px cells/16px icons and a 24px timeline section; larger
  windows use 28px cells/20px icons and a 28px timeline section.
- Controls stay on one row. Buttons that do not fit move to a native Windows
  overflow menu, retaining enabled/checked states and the configured commands.

## Existing behavior retained

Native toolbar command-UI updates run explicitly for the hidden backend. Commands
are sent to the original handlers. Audio/subtitle/custom dropdowns are anchored to
the overlay button. Native right-click alternate actions are retained.
Timeline and volume mouse input are mapped to their native control geometry and
handled by the existing controls, including capture and seek-drag pause/resume.
Mouse-wheel volume is sent to the native volume control.
The original toolbar and seekbar HWNDs stay alive as hidden backends and no longer
reserve space or appear over the overlay. Other native panels are unaffected.
Hover is reconciled by mouse events and existing position/UI updates; no new timer,
global mouse hook, helper process or WinUI dependency is added.

## Separate test only

Extract to the existing separate test folder, keeping mpc-hc64.ini containing
[Settings]. Do not replace the installed player or Firefox protocol handler.
This is a Lite build, without internal codecs. Start with a local playable video
in windowed Minimal/Compact. Alternate exclusive-fullscreen video windows have
not been validated.

## Runtime acceptance still required

Check local playback, Play/Pause via mouse and keyboard, seek click/drag/release
outside the view, volume click/drag/wheel and mute, previous/next/stop/rate,
audio/subtitle dropdowns, options/fullscreen/playlist/custom buttons when selected.
Change native Toolbar Layout and confirm the overlay follows it; test removed
buttons, narrow-window overflow, hover exit and resize during playback.
Compilation alone does not establish real playback or visual correctness.

## Native alignment, alpha and time controls

The native nToolbarAlignment option drives left/center/right layout of the overlay
button/volume group. Icon bitmaps now use the original BGRA alpha or AND mask
instead of GDI+ Bitmap(HICON), avoiding black matte conversion. Header height is
40/44 pixels and the seek row is 24/28 pixels, following a common four-pixel grid.

Native status-bar formatting is split at its own separator: position/remaining
time to the left of the timeline, total duration to its right. Clicking either
label invokes the original remaining/elapsed toggle; right click opens the native
time-options menu. Label input and disabled-control releases are consumed rather
than falling through to video Play/Pause. A zero-volume slider selects the native
silent icon without changing fMute, while the original mute action is retained.

Seek-hover timestamp and preview integration are deferred to the next stage.
Runtime checks: alpha on light/dark video, native left/center/right alignment,
zero/mute/unmute icon, elapsed/remaining toggle on either label, precision/frame
time modes, narrow layout, and no video pause from label/disabled-control clicks.

## Feedback and fullscreen inactivity

Fullscreen controls fade over 240 ms after four seconds without input. Mouse
movement and keyboard actions restore them; unchanged synthetic mouse moves do
not restart the deadline, and captured slider gestures prevent hiding. Windowed
hover behavior is retained.

The title uses a 15/17px font and removes a terminal known media extension. Volume
percentage stays visible to the right of its slider. Normal command messages are
routed from COSD into a compact translucent status box below the title; debug
messages retain the native OSD path. Cross-thread messages retain the native
fallback. Initial filename feedback remains suppressed.

Toolbar-owned command UI and click handlers are routed through the actual toolbar
first, including Repeat and Fullscreen. All enabled icons use the same 78-percent
white base and full white hover; disabled icons remain dim.

Runtime checks: added Repeat/Fullscreen actions and hover; consistent Frame-step
brightness; volume percentage; compact Play/Pause messages with the native OSD
option on and off; title extension handling; fullscreen fade, wake, slider capture,
pause and return to windowed mode.
