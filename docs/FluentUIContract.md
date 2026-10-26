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

Preserve the stock CChildView::OnSize renderer notification even when WM_SIZE
arrives during the frame's deferred child placement. Refresh chrome for that
installed client size as well; do not suppress both updates until RecalcLayout
returns. Keep the final frame fallback for dock/overlap changes that do not send
WM_SIZE, and keep the existing pre-reveal surface preparation. No resize timer,
frame-rate cap or renderer implementation change is introduced. Re-test opening
media, docking, fullscreen return, text bars and live corner dragging.

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

## Hosted transport layout dependencies

A hosted toolbar is centred in its own client row. Its geometry signature must
not include ancestor screen position, frame size or frame-to-toolbar origin;
those values change before the row width during a left/top resize and throughout
slide motion. Keep frame-relative dependencies only for the actual frame-parent
native docking path. Native button rectangles and visibility still participate.
Do not unconditionally arrange a hosted toolbar in RecalcLayout before its final
row placement. Keep PrepareNativePresentation after placement and before reveal;
that surface revision check preserves fullscreen/hidden-resize correctness.

## Visual verification workflow

The user performs interactive visual checks. Do not launch the player, move or
resize windows, or drive the mouse for automatic performance/appearance checks
without separately agreeing with the user. Source review, CI tests, binary/hash
verification and installation in the agreed Desktop test directory remain the
normal unattended workflow. Keep FluentResizeTiming disabled for ordinary use.


## Corrected resize rollback baseline (2026-10-25)

The installed approved comparison baseline is 7ca9dfc, not 996f084/b9f9727.
The whole-source rollback to 996f084 was too broad: the user reports worse
resize behavior after removing the later improvements as a group. Restore
application source and styled build configuration exactly to 7ca9dfc. The last
viewport-order test b7e5435 is not retained. Keep heavy tracing disabled and
buffered resize timing off. The stored verified 7ca9dfc EXE is installed without
copying old settings or changing renderer/dependencies.

Future rollbacks must name exactly which changes are being removed and compare
against the accepted feature contract. Do not equate chronological age with less
workaround code or remove unrelated accepted behavior for a resize test.

Two local-file display issues remain separate and are NOT fixed by this restore:
- The current title tooltip is populated only by the stream programme-caption
  branch; local files receive an empty tooltip. Do not claim it was restored.
- The example local MKV has container Title='by Martokc', also retained in media
  history; native playlist label precedence can show that instead of filename.
  Playlist source code is identical in 996f084 and 7ca9dfc. Do not erase metadata,
  history or explicit playlist labels to disguise a presentation-policy issue.

No further live-resize experiments or GUI automation without a new user request.


## Resize visibility and shared captions, 2026-10-25

Keep 7ca9dfc application behavior except these requested changes. No renewed
resize-layout, renderer, timer or surface-publication experiments.

A single frame flag identifies the native SC_SIZE/WM_SIZING operation. Feed
hide requests to the existing native toolbar/panel hider; the chrome hosts use
normal Present(false) and the playlist uses normal HideEnabled fade. Keep the
flag until WM_EXITSIZEMOVE (and clear it after cancelled SC_SIZE), so hover,
input ownership and deadlines cannot reopen overlays while the user resizes.
Do not activate this on SC_MOVE or on WM_ENTERSIZEMOVE alone. Preserve selected
controls and playlist selection; on exit recompute ordinary hover/deadline
visibility rather than force every panel visible. Skip rebuilding hidden chrome
artwork inside the resize loop. Reuse the existing frameless resize-band width
as the border exclusion for hover activation; do not change nonclient input.

GetDisplayCaption is the presentation boundary for header text/tooltip and
playlist labels/tooltips. Ordinary local files show filename without extension,
not embedded Title/history metadata. Their tooltip adds native rounded duration;
the playlist retains its existing duration column, without duplicating it in
text. Stream programmes retain title-only header, time/channel tooltip and timed
schedule rows. Channel-heading identity and unnumbered first row are unchanged.
Preserve CUE/extractor labels and native fallback behavior for devices. Do not
mutate actual file tags, history, saved playlist labels or URL query strings.

CI cannot prove visual performance. The user checks resizing, cancellation,
move-only behavior, hover reveal/hide, local captions and schedule numbering.
No GUI automation or player launch by the agent.


## Early animated hide and label-only programme priority

User clarified that slow edge press works and immediate press/drag starts hide
late. Keep normal hide animation: do not substitute instant disappearance.
Classify hovering the existing resize band as an ordinary hide request. On the
stock view's resize button-down call BeginNativeChromeSizing before SC_SIZE;
use the same idempotent entry from frame SC_SIZE and WM_SIZING fallback. Start
host/playlist hide before native toolbar hider layout. Use the request timestamp
for host hide elapsed time so a late first WM_TIMER cannot defer its start.
No new timer, no sleep, no move/sizing emulation; stock resize input is retained.
Opening animation duration and timing remain unchanged.

Current-stream captions only: if the exact source has a known channel header
but zero structured intervals, choose the first same-source timed programme label
from the imported list. The source-bound label-only list is supplied with the
current programme first. Keep playback selection, labels and numbering intact.
The shared GetStreamDisplayCaption serves header and OSD. Hovering the channel
row still describes the channel. With structured intervals keep Current() and
its expired/overlap protection; do not substitute an arbitrary future programme.
Without a programme label keep the existing channel fallback. Label-only times
remain imported text; do not invent dates or intervals.


## Bare HLS to XMLTV test (2026-10-26)

Keep the accepted resize, painting, titlebar hit testing and all icon assets.
Only a current HTTP(S) .m3u8 with one exact shared source is eligible. Local
files, mixed playlists and source-bound imported browser guides are unchanged.
On the existing position update poll a background resolver, never perform HTTP
or XML parsing on the UI thread. Prefer source-filter service_name; otherwise
read one manifest chain and at most 512 KiB of one TS segment, validate SDT CRC,
and reject missing/ambiguous/unsupported service names. This bounded fallback
temporarily uses another HTTP media connection; it is not per-frame polling.

Use public https://iptvx.one/EPG_LITE, cache compressed bytes beside the portable
player under EpgCache for 12 hours, and parse with a forward-only gzip XML reader.
Never parse the website or use browser script context. Exact display-name match
or explicit provider service alias -> stable XMLTV ID; preserve regional variants.
The initial History PAN EURO HD_Virtual_ENC -> history mapping is an explicit
test mapping to History Europe and must be compared with the actual broadcast.
Require a unique current interval. Failed/expired/unmatched EPG leaves playback
and the existing fallback caption intact; no fabricated programme.

Apply completed results only to the exact still-selected source; cancellation
uses owned worker state with no CWnd/COM/UI pointers crossing threads. Do not
reopen media to fill the schedule. Retain native guide formatting: title only in
caption, local time and channel in tooltip, numberless channel heading and first
programme numbered 1. Imported sidecars retain precedence.
