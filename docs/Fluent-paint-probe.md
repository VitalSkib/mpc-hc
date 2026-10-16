# Native seekbar transparency checkpoint

The current CI artifact uses `MPC_EXPERIMENTAL_NATIVE_SEEKBAR`, not
`MPC_EXPERIMENTAL_ALPHA_OVERLAY`. The previous Fluent overlay is compiled out:
expect the original toolbar, title behaviour, OSD and all original settings.
This is a composition/behaviour checkpoint, not the finished Fluent design.

The actual `CPlayerSeekBar` HWND stays visible and receives input directly.
Its original paint body is moved unchanged to `PaintNative(CDC&)`. In the
probe, this painter renders into a 32-bit DIB and the same HWND presents it
through `UpdateLayeredWindow`. Native background-colour pixels become 25%
black; all other pixels remain opaque. This colour-based adapter is temporary,
not a final anti-aliased theme renderer. No copied input geometry, second
seekbar, new timers or custom hide policy are introduced.

Alpha is attempted only for loaded media with the modern theme, when the
native `ToolbarsCoverVideo()` policy already places toolbars over video, and
there is no dedicated fullscreen video window. Other modes keep native opaque
painting. Allocation/presentation failure falls back to opaque native painting
for the rest of that player instance and emits a debugger TRACE.

## Runtime acceptance

1. Use the standalone artifact; do not replace the installed player. Open a
   local video with a duration. Keep the native seekbar enabled.
2. Test fullscreen first. The seekbar background should show moving video
   through it while the native track/markers/text stay readable. A black strip
   or the desktop showing through does not pass the composition test.
3. Hover for native time tips and enable native seek previews. Click, drag,
   release outside the bar, and check native chapters/A-B markers when present.
4. Check configured auto-hide delay/policy, resize, fullscreen transitions and
   a second renderer. In windowed mode the native hide-windowed-controls policy
   must place controls over video for alpha to be enabled.
5. Check seekbar time-placement/height preferences and the opaque fallback in
   modes that do not cover video. No helpers or Firefox scripts are involved.

Source verification confirms the original painter and seek/hover/preview/capture
handlers are unchanged. A successful CI build alone does not establish runtime
alpha composition or behaviour. Those require the checks above.

---

## Previous overlay prototype (historical)

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
percentage uses the normal volume OSD message rather than reserving row space. Normal command messages are
routed from COSD into a compact translucent status box below the title; debug
messages retain the native OSD path. Cross-thread messages retain the native
fallback. The native Show OSD option gates compact feedback too. Initial filename
feedback remains suppressed.

Toolbar-owned command UI and click handlers are routed through the actual toolbar
first, including Repeat and Fullscreen. Standard artwork uses the installed
Segoe Fluent Icons regular font at 16px; no font is bundled. The native selected
image index chooses both normal and alternate glyph banks (including RepeatOff,
Mute, fullscreen and pane states). Unsupported/custom artwork retains its native
colors, image and alpha. No blur, glow or white tint is added. Tooltips are obtained
from the native state-aware tooltip handler. Frame-step uses a thin frame with a
small system Play glyph; ShuffleOff adds the off-bank slash to the system glyph.

Runtime checks: added Repeat/Fullscreen actions and hover; consistent Frame-step
brightness; volume percentage; compact Play/Pause messages with the native OSD
option on and off; title extension handling; fullscreen fade, wake, slider capture,
pause and return to windowed mode.

## Timeline and feedback styling

Played portions use blue; both thumbs have a 13px pale outer disc and a smaller
blue centre. The centre expands smoothly on hover/capture and returns when the
hover ends. Animation reuses the existing deadline timer only while transitioning.
Time labels brighten on hover and have Elapsed/Remaining or Total-duration tips
matching the actual native display mode.

Volume row width is restored to 60/84px, retaining native group centring. Volume
messages again follow the native command path, rendered in the compact feedback
box with a lighter 35-percent black background. Enable Show OSD to see them.

Verify Repeat on/off shape and tip, mute/unmute, enter/exit fullscreen, pane state,
custom artwork, thumb hover/release, endpoints/capture, time tips/toggle, volume
feedback with OSD on/off and fullscreen idle fade. Font glyph presence was checked
against the locally installed font; visual rendering still needs runtime testing.

## Native routing and tooltips

Overlay commands are posted to the frame's original command router. It calls the
toolbar and continues to the frame when an ON_COMMAND_EX handler returns FALSE.
This is required for Mute: toggling the setting/icon must also apply the audio
renderer volume. Checked state no longer paints a persistent button background;
only hover paints it.

Volume has a separate themed tracking tooltip, updated on hover and changes as
`Volume 100%`, independent of Show OSD. It remains while dragging, is anchored
above the actual overlay slider, and stops on leave/hide. It adds no timer. Close
also has a normal themed tooltip.

## Original toolbar artwork reference

Original SVG atlases: `src/mpc-hc/res/buttons16.svg`, `buttons24.svg`,
`buttons32.svg`, `buttons48.svg`, `buttons64.svg`. Each has 26 columns and four
rows: light enabled; light disabled/alternate; dark enabled; dark
disabled/alternate. Disabled rows double as alternate artwork for toggle commands.
Mute uses two separate columns. There are 25 configurable button commands,
including four custom actions, represented by 26 image columns.

| Column (one-based) | Artwork |
| --- | --- |
| 1 | Play |
| 2 | Pause |
| 3 | Stop |
| 4 | Previous item |
| 5 | Next item |
| 6 | Audio tracks |
| 7 | Subtitles |
| 8 | Decrease playback rate |
| 9 | Increase playback rate |
| 10 | Frame-step |
| 11 | Open media |
| 12 | Options |
| 13 | Fullscreen toggle |
| 14 | Playlist toggle |
| 15 | Shuffle toggle |
| 16 | Repeat toggle |
| 17 | Seek backward |
| 18 | Seek forward |
| 19 | File properties |
| 20 | Exit |
| 21 | Custom action 1 |
| 22 | Custom action 2 |
| 23 | Custom action 3 |
| 24 | Custom action 4 |
| 25 | Speaker on |
| 26 | Speaker muted |

Supply individual SVGs with a common 24x24 viewBox and separate true on/off shapes
for Fullscreen, Playlist, Shuffle and Repeat. Play/Pause and speaker/mute are
already separate columns. Disabled colors/opacity and multi-size atlases can be
generated during packaging. Keep accent colors separate from foreground colors.
The current system-font substitution must be switched to native theme artwork
when the replacement set is integrated.
