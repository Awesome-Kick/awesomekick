# Audit repairs

Repair pass for [AUDIT-REVIEW.md](AUDIT-REVIEW.md), based on desktop-build `1a6f1769`.
All 62 original audit IDs are accounted for below. Final sanitizer validation also found H9, documented below. Implementation and desktop verification are complete. This report accompanies the repair commit on `desktop-build`.
No hardware flashing was performed during the repair pass.

## Verification status

| Validation | Result |
|---|---|
| Original audit baseline | 196 engine checks passed. |
| Final Debug engine suite | **243 passed, 0 failed**, exit 0. |
| Final AddressSanitizer engine suite | **243 passed, 0 failed**, exit 0; no report from the repaired rerun. The initial run found H9 and its report is retained. |
| Simulator audit mode | **5 passed, 0 failed**, exit 0. |
| Simulator instrument / Modular | Both exit 0 and reach `SELFTEST done` on the final build. |
| Patch whitespace check | Passed. |

The 47 checks beyond the original baseline cover the reproduced memory/lifetime failures, note undo and concurrent snapshots, live routing, first-tick and delayed ratchet scheduling, sustained/retriggered notes, MIDI batches and clock timestamps, slice restore and reverse/fractional rendering, firmware/UART bounds, preset collisions, write failure, exact edit undo/redo, unreadable preview, and song MIDI export.

The graph crossfade intentionally changes structural-edit transients. The existing spectral comparison resets after installing its patch so it measures settled timbre; its comparison and tolerance are unchanged. The export fixture explicitly uses the sequence length, avoiding an incorrect assumption about how many times a shorter track repeats.

Desktop DSP workload: one second of 16-voice oscillator/ladder/drive/delay plus chorus took **344.05 ms in Debug** (218.79 ms in RelWithDebInfo with ASan). This is a development-build workload measurement, not an ARM deadline or individual-function profile; it does not justify replacing accurate DSP functions with approximations.

Local logs and result snapshots are in `../devtools/audit-fixes-*`; test result files remain beside their executables. Use `KICK_TEST_AUDIT_ONLY=1` for the 44 audit checks, or run the ordinary suite for all 243. Use `KICKEM_SELFTEST=audit` for the five shared UI lifetime/empty-state checks.

## Programs and DSP

| IDs | Change / disposition |
|---|---|
| A1 | Preserve physical held keys separately from arp-owned voices; switch arp routing without orphaning a direct voice; route carried keys through the new arp. |
| A2 | Release voices and clear mono held state when play mode changes. |
| A3 | Reset engines and note bookkeeping on prepare, including the same sample rate; reset arp and held state. |
| A4 | Inspect every voice-mix sample; stop runaway voices on nonfinite output or absolute amplitude above 32. |
| A5 | Delay reset invalidates ring contents in constant time; unwritten positions read as zero. |
| A6 | Add the normalized filter-envelope amount and filter-LFO amount independently. Pitch-LFO depth no longer moves cutoff. Stored envelope depth uses the MIDI scale. This intentionally changes affected patches' sound. |
| A7 | Crossfade structural graph changes over 20 ms, retaining the previous graph for release/effect output and destroying it on the editing thread. This smooths the transition; it does not preserve an arbitrarily long delay/reverb tail. |
| A8 | Retain fractional arp countdown overshoot. |
| A9 | Remove obsolete deferred releases when an input note retriggers. |
| A10 | Zero cached cable samples beyond a short arp sub-block. |
| A11 | Retry graph preparation outside the audio lock if the sample rate changed during construction. |
| A12 | Profiling candidate, not an established defect: retain accurate sine/tanh/filter functions. An explicit 16-voice oscillator/ladder/drive/delay/chorus desktop workload now reports elapsed time. ARM profiling and sonic comparisons are required before choosing approximations. |
| A13 | Queue CC64 sustain with note events; pedal-up releases sustained keys, including through a muted track. All-notes-off clears the pedal state. |
| A14 | Preserve presets whose names sanitize to the same filename; replace matching named patches transactionally; check the patch tag before deletion. |

## Engine and persistence

| IDs | Change / disposition |
|---|---|
| E1 | ValueTree listeners update the playback index for add/remove/property edits, undo, redo and direct tree operations. |
| E2 | The playback index contains copied plain note data behind one lock; playback never retains editable note/map pointers. |
| E3 | Share a sequence lifetime lock with transport processing; detach the actual playing sequence before deletion; invalidate queued numeric sequence references. Stop clears the playing pointer. Unrelated playback can continue when another sequence is deleted. Clock callbacks publish ticks; transport graph work runs on the tick worker. |
| E4 | Remember the destination note/channel for every live input key. Note-off and aftertouch use that destination after routing/transpose changes; stop flushes all destinations. |
| E5 | Schedule the inclusive starting boundary, including tick zero and seek/loop offsets. |
| E6 | Schedule ratchets from each note's actual tick and subdivision boundaries; consume queued events in chronological order across delayed tick batches. Bound subdivisions by note duration. |
| E7 | Validate readers/streams; propagate read/write/flush errors; replace audio through checked temporary files. Undo/redo own exact before/after snapshots; eviction no longer rewrites a shared backup. A failing output stream test verifies the original survives byte-for-byte. |
| E8 | Reject unreadable preview sources; guard publication/rendering with one source lock; clamp ranges; fix reverse block ranges, destination offsets and millisecond conversion. |
| E9 | Owning value containers replace raw allocated note/map objects. |
| E10 | Use the MIDI-port setter argument. |
| E11 | Scope track/program and sequence/track-name signal subscriptions; disconnect before track/sequence teardown. Scope manager name subscriptions as well. |
| F1 | Clear dispatched MIDI batches; snapshot input names while the MIDI source is alive so hotplug cannot invalidate subsequent routing reads. |
| F2 | Reverse starts on the last valid sample and subtracts its step; bound interpolation taps and robustly wrap large steps. Preserve fractional position through the final sample. Empty/unreadable slices stay silent. |
| F3 | Distinguish binding a saved source from replacing it; preserve and validate persisted trim boundaries on restore/copy. |
| F4 | Append four distinct layer nodes. |
| F5 | Add an explicit persisted Internal/MIDI Input clock choice. Route Clock/Start/Stop/Continue from the selected input; correct units and timestamp order, preserving input reception timestamps across message-thread batching; guard empty histories, invalid PPQN and invalid tempo; keep external timing driven by received clocks. |

## UI and file operations

| IDs | Change / disposition |
|---|---|
| U1 | Dispatch the complete learn operation once to the message thread using a safe component pointer. Local program/bus/selection state is resolved and used synchronously there. Remove seven callbacks retaining stack locals and guard absent program types. |
| U7 | Reject invalid/empty/path-containing project names and existing destinations in both new-project entry points and template copy. Check template presence, directory creation and copy success before loading. |
| U8 | Trim owned table rows on shrink, including empty/hidden tables, and clamp/clear selection. |
| U9 | Bounds-check recent-item clicks/highlighting/scrolling after removal or with an empty installation. |
| U10 | Marshal project-loaded preference and animation/navigation updates onto the message thread. Prevent a second loader start while its worker runs. |
| U11 | Populate row metadata immediately. Calculate folder sizes on a bounded worker pool; reject stale/deleted row callbacks. Use 64-bit totals and one recursive scan, without following symlinks. Stop the loading overlay after directory enumeration, independently of size jobs. |
| U12 | Copy only new recorder samples under the recorder's buffer lock and add incremental thumbnail blocks at the actual sample rate. Reset thumbnail state when recording changes. |
| U14 | Protect the active project and its ancestors by resolved path ancestry; revalidate at confirmation time. |
| U15 | Read the mono meter's valid channel/peak and synchronize the reverse button. |
| U16 | Implement sampler Export/Clear. Song Export explicitly exports MIDI notes, tempo/time signature and sequence repeats into the project Exports folder, with checked file creation and feedback. Recorder save now propagates errors and uses transactional replacement; failed export retains the recording. |
| U17 | Guard the selected program before assigning a saved recording. |
| U18 | Free split/rename dialog content on Cancel as well as Apply; fix the updater's always-true status expression. |
| U19 | Capture delete-dialog ownership safely; snapshot selected paths; revalidate the current project; avoid indexing an empty refreshed directory. |
| P1 | Apply message-thread list selection synchronously; deferred selections use safe pointers and revisions. Rebinding invalidates queued selections. |
| P2 | Use one inline theme signal across translation units and weak component subscriptions that disconnect after owner destruction. Look-and-feel owns a scoped connection. Also bound cyclic-state normalization at 1.0 and handle empty states. |
| P3 | Return safely from empty mixer knob handlers. |
| P4 | Map filtered MIDI row indexes to sequence track indexes and back. |
| P5 | Remove deselected pads from the shared edit selection. |

## Hardware, update flow and IPC

| IDs | Change / disposition |
|---|---|
| U2 | Route SYSTEM software-update replies before normal navigation. Replace the MCU busy wait with an event wait and 10-second acknowledgement timeout. |
| U3 | Copy update status into safe message-thread callbacks; own popup pointers by value. Image validation and flashing run outside the message thread. |
| U4 | Register scan completion before starting; keep populated device data alive; snapshot the selected device name before confirmation. |
| U5 | Suppress hardware navigation on the update screen and guard concurrent firmware workers centrally; avoid re-launch on visibility callbacks. Stop the USB timeout timer and ignore late USB status once flashing takes ownership. |
| U6 | Use synchronized normal reboot instead of forced no-sync reboot. |
| U13 | Clear hidden update-view USB callbacks; header Back stops USB; queued view callbacks verify visibility. |
| H1 | Bound Intel HEX records and output buffers, verify hex digits/count/checksum/record shape, require EOF, and preflight the whole MCU file before initiation. Correct the extra transmitted byte and reset per-run state. |
| H2 | Reserve the header terminator and reject marker indexes beyond the fixed header. |
| H3 | Receive exactly the announced bytes across short/EAGAIN/EINTR reads with a timeout; write only received bytes; bound chunks and total length; reset counters per transfer; require length/fsync/close success before completion. |
| H4 | Device-selected staging directory, basename-only image destinations, size limits, no-follow/exclusive temporary creation and checked final rename. Parse numeric sizes without uncaught conversion exceptions. |
| H5 | Retain fragmented UART headers/payloads; use unsigned lengths; validate command payload minimums before dispatch and guard contract helper access. |
| H6 | Return process names by value. |
| H7 | Remove only the temporary unregistered socket entry after logical registration. |
| H8 | Publish JSON with its UTF-8 byte count. |
| H9 (found during repairs) | The audio graph traversed bus buffer vectors concurrently with routing changes that reallocated them. ASan reported a null-slot access in `AudIOBuffer.h:275` from `Bus::process`. Rendering now tries the existing graph mutation lock and clears the block if an editor owns it; preparation uses that lock too. A live 48-program bus-growth stress case covers the path. This can briefly silence a block during edits instead of blocking the audio thread. |

## Additional compiler-confirmed lifetime repairs

- Return asset display names by value instead of a reference to a temporary.
- Return owned audio-clip wrappers instead of pointers to stack objects.
- Fix the bounded-follow-up CV calibration lookup to use note indexes relative to the validated channel root. Physical CV acceptance remains pending.

## Validation limits

Hardware flashing, USB gadget endpoints, UART devices, CV output and Linux multi-process recovery are not exercised on this Windows host. Portable parsers and destructive-file operations are tested with disposable synthetic fixtures. Hardware and ARM timing acceptance remain required; a Windows sanitizer pass is not a Linux/device certification or a leak checker.

The publishing restrictions in AUDIT-HANDOFF.md still apply: push only to `fork` when explicitly requested, no upstream PRs, no ASIO builds/SDK or personal/commercial assets in distribution, and distribution archives only from `Kick/build/dist`.
