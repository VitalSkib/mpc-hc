# Fork audit, 2026-10-30

## Scope and limits

The review covered the accepted d7ef4ad baseline and all 54 changed files relative to common ancestor 48478c2. Background work, windows/subclasses, timers, GDI/COM/Composition, caches, updating and layout were reviewed separately. This audits our changes; it does not claim that all inherited MPC-HC code and libraries are defect-free. CI performs compilation and tests. Heap/GPU profiling, long-running stress and automated visual checks on the user's desktop were not performed.

The rejected a1c7e4a WinUI-menu experiment is excluded. The UI contract is based on d7ef4ad. Official stable 2.8.4 was incorporated through a conflict-free merge.

## Findings and fixes

| Area | Result |
|---|---|
| EPG worker | One active and one replaceable pending task. Cancellation on source changes and destruction. The worker sleeps on a condition variable rather than spinning. Cancellation is cooperative; synchronous HTTP waits for its timeout. |
| Imported schedules | Imported guides have priority. Background XMLTV identity learning cannot replace them. A signature prevents learning from restarting on every tick. |
| HTTP/XML/gzip | Bounded input size, HLS recursion, reading time, inflated data and XML-entity expansion. No external XML resolver. Scoped owners release COM/gzip resources. |
| Cache | One XMLTV file up to 40 MiB, refreshed on demand after 12 hours; identities up to 2 MiB/512 entries, with old entries pruned. New data is validated before replacement. Failed flush/close operations are now checked before replacing files. |
| GDI exceptions | Manual cleanup in playlist snapshots and decoration could be skipped on exceptions. Stack owners now restore/release DCs, bitmaps, painting flags and temporary list properties. Normal artwork is unchanged. |
| Buffered paint | Balanced cleanup after failed CreateEx and errors/exceptions between BeginBufferedPaint and EndBufferedPaint. |
| Timers | Chrome 1 is transition-only. Playlist 2/4/5 serve snapshots/slide/fade and stop on completion; 3 is a coalesced one-shot update. Playlist 1 watches hover every 100 ms while enabled. Thumb 0x7f31 serves short transitions only. HWND destruction removes remaining window timers. |
| Paint feedback | List changes schedule one update; read-only queries do not. Unchanged pixels are not republished to Composition. No continuously self-triggering paint loop was found in this path. |
| Subclasses/windows | Native child controls return to their owners. Handlers detach on teardown/WM_NCDESTROY. Disabling material retains the native fallback. |
| Composition | State releases Acrylic/Mica, target and compositor. The UI DispatcherQueue and bootstrap are intentionally retained for process lifetime; their number does not grow on every panel show. Dynamic GPU stress was not performed. |
| Diagnostics | Heavy per-present/per-message diagnostics are not enabled in release flags. Obsolete experimental headers are not treated as supported features. |
| Updater | Retains the native checker and dialog. URLs point to the fork without falling back to the official update server. Added scoped HTTP-file ownership and a bounded version response. |

## Tests

- Existing programme, HLS/XMLTV, lifecycle, identity, scrolling and frame-input tests remain required.
- A Win32 test throws 1,000 exceptions while bitmap/DC ownership is active, checking that GDI counts and painting flags are restored.
- Temporary-file update tests cover INI/EPG preservation, routing hashes, invalid SHA256 rejection, EXE rollback on routing-write failures and retention of three backups.
- Release tests separate stable/beta channels and validate tags and revisions. Source guards protect retained UI features and exclude the rejected WinUI integration.

## Remaining limitations

Static review cannot establish the absence of all leaks. Previously intermittent Statistics labels and fullscreen transitions remain under observation. Allocation/device failures may select the fallback background. Power loss can leave a temporary file; arbitrary user temporary files are not automatically deleted. Ambiguous or unidentifiable HLS streams do not receive invented titles. Concurrent independent player instances use last-writer-wins identity persistence rather than a merged cross-process database.
