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
- Small windows use 24px cells/16px icons and a 30px timeline section; larger
  windows use 28px cells/20px icons and a 34px timeline section.
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
