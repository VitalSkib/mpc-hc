# MPC-HC Fluent documentation

This Windows x64 fork retains native MPC-HC playback and commands while adding a Fluent video interface and live TV programme information.

## Features

- Animated titlebar and playback controls over video, custom artwork and shortcut tooltips.
- Native Status/Statistics rendering, window dragging and resizing.
- An Acrylic playlist with coordinated animation and scrolling. Channel headings are unnumbered; programme rows start at 1.
- Controls hide during resizing and adapt to small windows.
- Imported programme guides take priority. Bare HLS links can use stream metadata, XMLTV and confirmed cached identities. Unidentifiable streams may remain unnamed.
- Local files keep normal filename-based labels. Native context menus are retained.

## Windows installation and updates

Download **MPC-HC-Fluent-...-x64-setup.exe** from [Releases](https://github.com/VitalSkib/mpc-hc/releases/latest), close MPC-HC and run setup. No original installation is required. The package includes the full player, LAV Filters, MediaInfo, MPC Video Renderer, languages and Fluent artwork. Use the normal Start menu shortcut after installation; uninstall through Windows Installed apps.

The installer uses MPC-HC x64's existing product identity, so it upgrades an existing x64 installation. Native profile storage and file-association behaviour are retained; default applications are not changed without user choice.

**Use Fluent appearance** applies Minimal view, whole-window cursor-movement activation, a four-second hiding timeout and the Custom Fluent toolbar through native settings. The separate Reset settings task is unchecked by default. Upgrades preserve other preferences. Incomplete hover defaults from early full installers are repaired once, independently of the selected icon theme; subsequent deliberate hover-policy changes are preserved.

The offline package includes Microsoft-signed Windows App Runtime 1.8.12 for Acrylic. On supported Windows it is installed only if a compatible runtime cannot be activated. Other applications are not forcibly terminated. This shared component is retained when the player is uninstalled. Older unsupported Windows versions use the existing fallback background.

Installed EPG data is stored in `%LOCALAPPDATA%\MPC-HC\EpgCache`; writes to Program Files are unnecessary. Personal INI files, history, browser routing helpers and stream credentials are not included. Setup does not reconfigure browser scripts.

The native update checker points to this fork. Stable and beta builds read separate `updates-stable/version.txt` and `updates-beta/version.txt` feeds. Installation remains manual.

## Release notifications

Enable **Watch > Custom > Releases** on GitHub and email delivery in your GitHub notification settings. The project does not store your email address or change account preferences. The native update checker is another way to find new releases.

## Stable and beta channels

The scheduled monitor follows stable `clsid2/mpc-hc` releases. Beta builds require a manual run of **Monitor official MPC-HC releases** with channel `beta`. They are published as prereleases and do not replace the stable branch or feed.

## Automated builds

`fluent-upstream.yml` runs every six hours from `develop`:

1. Compare the latest official release with the last published fork release.
2. Fetch the exact upstream tag and prepare a separate `auto/...` branch.
3. Perform a normal merge. Conflicts stop the workflow for manual review.
4. Run the retained tests, full x64 build, languages and native Inno Setup. A disposable runner verifies fresh installation, codec loading, runtime activation, profile-preserving upgrades and uninstall.
5. Publish the complete EXE, optional portable update ZIP, SHA256 sums and a source/dependency manifest. Stable promotion requires the base branch to remain unchanged during the build.
6. Update the player feed only after the files are public. Incomplete drafts are not offered as updates.

An unchanged upstream release does not trigger compilation. Published version numbers are never overwritten. The fourth version component is the fork build revision; it increases across direct and reusable workflow runs. Automation uses the scoped GitHub Actions token, not a personal access token.

## Optional update for an existing portable folder

The `-update.zip` is supplementary. Use the EXE installer for normal Windows installations and upgrades.

1. Download and extract the update ZIP; optionally verify it against `SHA256SUMS.txt`.
2. Close the player and open PowerShell in the extracted folder.
3. Run `./Update-Fluent.ps1 -TargetDirectory "C:\path\to\portable-player"`.

The EXE is replaced atomically. Settings, icons, libraries, EPG and associations are preserved. Only the integrity hash of an existing test browser router is updated. Three EXE backups are retained in `.fluent-backups`. No player is killed or launched. Routing-write failures restore the previous EXE. After an interrupted update, check the process and backup before manually removing a leftover `.fluent-update.lock`.

## Source, dependencies and validation

Each manifest identifies the exact source commit and upstream tag. Clone that commit recursively for corresponding sources; see [Compilation](docs/Compilation.md) and the pinned workflow dependencies. Released codecs and additional libraries come from the matching official MPC-HC package. `dependencies.json` and the release manifest record URLs, SHA256 hashes and source references. The stock EXE used for installer syntax checks is replaced by our full build; packaging rejects an incorrect build revision. Inno Setup 6.7.3 is used only as a build tool.

The accepted UI baseline is `d7ef4ad82d0340386bab0f8a905ebeb9ee6aa1ef`. The [UI contract](docs/FluentUIContract.md) and [audit](docs/FluentAudit-2026-10-30.md) document retained behaviour and validation limits. CI does not prove the absence of every visual issue or leak. Visual confirmation remains a user test.

MPC-HC and fork modifications are distributed under [GPLv3 or later](COPYING.txt). Upstream authorship and attribution are retained.
