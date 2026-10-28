# Completed code review (2026-10-27)

Subsequent implementation and validation are tracked in [AUDIT-FIXES.md](AUDIT-FIXES.md). This report records the unchanged audit baseline.

Reviewed `desktop-build` at `1a6f1769`. This completes the risk-based source review described in [AUDIT-HANDOFF.md](AUDIT-HANDOFF.md). No production source was changed. No commit, push, release, or hardware update was performed.

The result is **30 additional findings**, alongside the **32 inherited findings** rechecked below. Eleven findings, including A1 and A2, were reproduced with probes linked against the unchanged engine objects. Three of those reproductions produced AddressSanitizer errors.

"Source-confirmed" means the cause and a concrete failure path were checked in the source; it does not mean the scenario was exercised on hardware. Hardware flashing, USB gadget transfers, UART input, physical CV, and multiprocess IPC were reviewed statically. Third-party DSP libraries, generated code, the kernel, and the hardware bootloader were outside this pass. This is a review of the six agreed areas, not a claim that every possible execution path is defect-free.

## Coverage

| Area | Completed work |
|---|---|
| Programs and Modular engine | Rechecked A1-A14; reproduced A1 and A2; traced slice rendering and program/track lifetimes. |
| Main `kick_ui` | Rechecked U1-U18, including update routing and ownership; added U19. |
| `engine/audio_engine` | Transport/thread lifetime, sequences/tracks, note editing and undo, recording, sample editing/player, buses and inserts; E1-E11. |
| Engine support and `_playback` | File/cache/slice ownership, MIDI delivery/clock, interfaces, project persistence, grouping, sampler wrappers and playback app; F1-F5. |
| Hardware/platform/apps | Firmware parsing/transfer, UART and contracts, IPC transport/process management, CV/I2C, compatibility audio/recorder, common helpers, LEDs, resource metrics and app integration; H1-H8 plus observations below. |
| Program editor and shared UI | Modular editor callbacks/pickers, sample editors, mixers, row views, controls, selectors/header, settings and resources; P1-P5. |

## Validation and reproducible evidence

| Check | Result |
|---|---|
| Debug engine build and existing suite | **196 passed, 0 failed**, exit 0. |
| ASan engine build and existing suite | **196 passed, 0 failed**, exit 0; no ASan report from this baseline run. |
| Debug simulator build | Passed. |
| Simulator `instrument` self-test | Exit 0; log ends `SELFTEST done`. |
| Simulator `modular` self-test | Exit 0; log ends `SELFTEST done`. |
| Separate logic probes using existing Debug objects | Eight distinct findings reproduced: A1, A2, E1, E10, F1, F3, F4, H8. |
| Separate probes using existing ASan objects | F2: heap-buffer-overflow; H2: stack-buffer-overflow; E11: heap-use-after-free. Each expected-failure run exited 1. |

Passing the existing suite does not cover the new failure paths. Its clean ASan result must not be described as a clean result for the added probes.

Local evidence is in [`../devtools/audit-2026-10-27/`](../devtools/audit-2026-10-27/), outside the repository:

- `audit_probe.cpp` and `build_probe.py`: standalone harness, linked to existing build objects without recompiling or altering production sources. The MIDI probe exposes the existing private JUCE callback interfaces with explicit test-only base casts.
- `probe-results.txt`: MIDI batches `60 | 61 | 60,62`; undo-add `tree=0 playback-map=1`; undo-delete `tree=1 playback-map=0`; MIDI-port request ignored; one layer instead of four; restored slice `0..128` instead of `12..80`; truncated Unicode payload; one gated Modular voice remaining after each release scenario.
- `reverse_asan_report.*`, `header_asan_report.*`, `track_asan_report.*`: symbolized memory-error reports.
- `sim-instrument.log`, `sim-modular.log`: simulator traces. Engine suite results also remain in the corresponding build folders as `engine_test_results.txt`.

Preferences were isolated with `KICK_PREFS_DIR`. The simulator tests were run one at a time. Only synthetic audio and temporary projects were used in the added probes. Existing simulator instances were not terminated.

Rebuild the standalone harness with `python ../devtools/audit-2026-10-27/build_probe.py` (add `--asan` for the ASan objects). Its optional modes are `reverse`, `firmware-header`, and `track-lifetime`. The last two test a parser and local object destruction; neither performs a hardware update. The helper currently uses this machine's installed MSVC/SDK paths. These are historical pre-repair harnesses: the note-index API has since changed. Use the permanent `KickEngineTest` audit cases on the repaired tree; reproducing the original probes requires the baseline checkout/build.

**ASan command correction:** run from the ASan build directory with its runtime DLL directory on `PATH` and `ASAN_OPTIONS=log_path=asan_audit_report:halt_on_error=1`. An unquoted absolute Windows drive path is parsed as additional options, and the options separator is `:`, not the handoff's comma.

## Additional engine findings

Paths in findings are relative to `Kick/modules/` unless explicitly stated.

### E1 HIGH: note undo/redo does not synchronize the playback index — reproduced

- **Where:** `engine/audio_engine/engine/mt_audio_note.cpp:431-463,530-548,670-699`; `mt_audio_note.h:145-151,208`.
- **Scenario:** add a note, undo, then play; or delete a note, undo, then play. The visible/saved `NOTELIST` changes, but playback reads a separate nested `notes` HashMap that only the editing methods update. There is no listener rebuilding it for undo/redo. Direct tree edits in `modifyLength()` and `explode()` have the same problem.
- **Evidence:** undoing an addition leaves zero tree notes and one indexed note; undoing deletion leaves one tree note and zero indexed notes. The latter can also make a later `eraseNote()` dereference a missing map.
- **Fix direction:** make note-tree events the single synchronization path, including movement between start-tick buckets, undo, redo and direct edits. Avoid updating two stores through separate partial paths.

### E2 HIGH: playback and note deletion use different locks

- **Where:** `engine/audio_engine/engine/mt_audio_track.cpp:285-328,347-361`; `mt_audio_track.h:154`; `mt_audio_note.cpp:530-548,680-699`; `mt_audio_note.h:200,208`.
- **Scenario:** erase or move notes while a sequence plays. `MTTrack::onTick()` holds its own `lock`, while `MTNoteList` editing uses a different member also named `lock`; some mutations do not lock at all. Playback holds raw map/note pointers that the UI can remove and delete.
- **Effect:** iterator invalidation, use-after-free, or corrupt scheduling during ordinary live editing.
- **Fix direction:** use one ownership/synchronization scheme for the playback snapshot and mutations. Do not assume the same lock name means the same lock object. Add a concurrent edit/playback stress case.

### E3 HIGH: deleting a sequence leaves the transport's playing pointer dangling

- **Where:** `engine/audio_engine/engine/mt_audio_manager_sequence.cpp:220-247`; `mt_audio_manager_transport.cpp:181-192,284-306`; `mt_audio_manager_transport.h:85`.
- **Scenario:** play sequence A, delete A, then stop again, or select B while A plays and delete A. Deletion checks the selected sequence rather than the playing sequence. Even when it calls `manager.stop()`, `stop()` never clears `playingSequence` before the sequence is erased.
- **Effect:** later transport operations or an in-flight tick dereference freed sequence memory. The tick worker and UI also access that raw pointer without a shared lifetime guard.
- **Fix direction:** stop and detach the actual playing sequence under synchronization, wait for in-flight work, and invalidate queued sequence references before deletion.

### E4 HIGH: changing track/sequence transpose while a live key is held loses its release

- **Where:** `engine/audio_engine/engine/mt_audio_track.cpp:111,381-409`; `mt_audio_track.h:114`.
- **Scenario:** press MIDI note 60, raise track transpose by 12, release note 60. The note-on was sent as 60, but the note-off is recalculated as 72. The instrument's own transpose bookkeeping cannot repair this earlier remapping.
- **Effect:** a gated instrument/keygroup can sound until all-notes-off. Changing the MIDI output channel while held has the analogous pairing problem.
- **Fix direction:** retain the destination note/channel for each active input note and release that exact destination, or explicitly flush before routing/transpose changes.

### E5 MED: notes at tick zero are skipped on the first playback pass

- **Where:** `engine/audio_engine/engine/mt_audio_track.cpp:193-202,285-292`; `engine/audio_midi/source/CMidiClock.cpp:231-281`.
- **Scenario:** place a hit at the sequence start and press Play Start. `prevTick` begins at zero and `fillMidiMessages()` starts at `PrevTick + 1`, so initial playback never visits tick zero. Modulo wrapping visits it only after a full track length.
- **Fix direction:** explicitly schedule the start boundary and define inclusive/exclusive tick intervals consistently, including seek and loop starts.

### E6 MED: the first ratchet release is queued before its note-on

- **Where:** `engine/audio_engine/engine/mt_audio_track.cpp:309-324,350-361`.
- **Scenario:** a note at tick 96 with length 96 and four ratchets. `time` starts at the current `tickCtr`; the first release is queued for `time - 1` before `time` advances to the first subdivision.
- **Effect:** the initial hit is cut on the next tick callback, leaving a gap until the second ratchet. When a callback spans several ticks, use of the callback-wide `tickCtr` also shifts subdivision timing away from each note's actual start.
- **Fix direction:** calculate every on/off from the note's absolute scheduled tick and its subdivision boundaries; test first-hit duration and delayed tick batches.

### E7 HIGH: destructive sample edits can replace the original after a failed write

- **Where:** `engine/audio_engine/engine/mt_sample_editor_actions.cpp:224-241,271-286,309-354,359-389`.
- **Scenario:** apply an edit while the destination disk fills or a stream write fails. Return values from audio writes and the final move are ignored. Cleanup deletes `sourceFile` before confirming replacement succeeded.
- **Effect:** the original sample can be replaced by incomplete audio or lost, while `execute()` reports success. Failed reader creation is also dereferenced inside initialization before the caller's null check.
- **Fix direction:** validate reader/writer creation, propagate write/flush errors, and commit through a checked temporary-file replacement that preserves the original on failure. Test disk/error handling using disposable files.

### E8 HIGH: previewing an unreadable sample dereferences a null reader

- **Where:** `engine/audio_engine/engine/mt_sample_player.cpp:23-37,57-112,150-165`; `mt_sample_editor_interface.cpp:362-386`.
- **Scenario:** select an existing corrupt/unsupported audio file and press preview. File existence creates a transport, but reader creation can fail. `start()` uses `reader->lengthInSamples` before any effective guard, including the default full-sample path.
- **Effect:** message-thread crash; an invalid active source can also reach the audio callback.
- **Fix direction:** reject an unreadable source before attaching/starting it and make start/render safe without a valid reader. Clamp region/read ranges as part of that contract.

### E9 MED: note objects leak when a track or full note list is cleared

- **Where:** `engine/audio_engine/engine/mt_audio_note.cpp:79-86,439-448,670-676`; `mt_audio_note.h:32,208`.
- **Scenario:** repeatedly load projects with many notes or use Clear All. Nested maps and `MTNote` objects are allocated with `new`. `notes.clear()` and the default destructor destroy pointer values, not the pointed-to objects.
- **Effect:** memory grows across editing/reload sessions. Per-note erase frees these allocations, but full-list/track teardown does not.
- **Fix direction:** use owning containers or one complete teardown path. Windows ASan's successful baseline is not a leak check.

### E10 MED: the MIDI-port setter ignores its argument — reproduced

- **Where:** `engine/audio_engine/engine/mt_audio_track.cpp:112`; `mt_audio_interface.cpp:1240-1243`.
- **Scenario:** call `setTrackMidiPort(..., "Port B")`. The implementation assigns `midiPort` back to itself instead of assigning `MidiPort`.
- **Evidence:** the probe requested `AUDIT_PORT` and read back the previous empty value.
- **Scope:** the inspected track/mixer UI writes the cached property directly, so this is specifically a broken public setter; it does not establish that those selectors currently fail.
- **Fix direction:** use the supplied argument and add an API round-trip check.

### E11 HIGH: deleted tracks remain subscribed to program-deletion signals — ASan reproduced

- **Where:** `engine/audio_engine/engine/mt_audio_track.cpp:69-75`; `mt_audio_track.h:40`; `mt_audio_manager_program.cpp:148`. Related sequence subscription: `mt_audio_sequence.cpp:72`.
- **Scenario:** create/assign a program, delete its track, then delete a program. The `programBeingDeleted` slot captures the track's raw `this`, and track destruction does not disconnect it.
- **Evidence:** ASan reports heap-use-after-free at `mt_audio_track.cpp:70` during `deleteProgram()`. `MTSequence` similarly retains an unscoped `trackNameChanged` callback after destruction.
- **Fix direction:** own the signal connections with scoped connections or tracked lifetimes, and disconnect before object teardown. Extend coverage to reload followed by rename/deletion.

## Additional support-engine findings

### F1 HIGH: processed MIDI messages are replayed in later batches — reproduced

- **Where:** `engine/audio_midi/source/midi_device_manager.cpp:289-317`.
- **Scenario:** three separate MIDI batches arrive. The handler swaps `incomingMessages` and `midiMessages`, dispatches the latter, and never clears it. At the next swap, processed messages return to the incoming queue.
- **Evidence:** input batches 60, 61, 62 are dispatched as `60 | 61 | 60,62`. The arrays keep accumulating earlier messages on alternate batches.
- **Effect:** repeated notes/CC actions, mismatched note releases, growing work and retained stale input-device pointers.
- **Fix direction:** consume each batch once, clear the processed batch before reuse, and test interleaved producer arrivals and hotplug lifetime.

### F2 HIGH: reverse sample rendering advances forwards past the buffer — ASan reproduced

- **Where:** `engine/audio_file_manager/source/audio_sound_renderer.cpp:191-205,232-255`; `audio_sound_renderer.h:68-79`; `audio_sound.h:105`.
- **Scenario:** play a normal 128-sample slice in reverse. Both directions compute `newPos = curPos + positiveRate`; reverse starts at the exclusive loop end and only checks the lower bound. Its interpolation coordinates have no upper clamp.
- **Evidence:** repeated `ASliceRenderer::getNext(1.0)` causes a heap-buffer-overflow in `interpolateSample()` at `audio_sound_renderer.h:77`.
- **Fix direction:** start reverse playback on a valid sample, subtract the step, and wrap/clamp both interpolation taps, including empty/one-sample slices and pitch steps larger than a loop.

### F3 HIGH: restoring or copying slices resets saved trim boundaries — reproduced

- **Where:** `engine/audio_file_manager/source/mt_audio_asset_manager.cpp:146-155,329-345`; `audio_sound.cpp:136-167,199-209`.
- **Scenario:** load a project with trimmed slices, or add a copied slice. The slice attaches to the saved tree, then `setSource()` unconditionally writes `loopStart=0` and `loopEnd=INT_MAX` when attaching its source.
- **Evidence:** a tree containing start 12/end 80 becomes start 0/end 128 after attaching a 128-sample sound.
- **Effect:** slice/trim boundaries are lost in memory and the same tree can subsequently be saved. This affects restoration as well as copying.
- **Fix direction:** distinguish binding an existing source during restore from explicitly replacing a source; preserve and validate existing boundaries during binding.

### F4 MED: newly created samples have only one persistent layer — reproduced

- **Where:** `engine/audio_engine/engine/mt_audio_interface_types.cpp:148-164`.
- **Scenario:** create a new drum/keygroup/clip sample and assign layers 2-4. The loop calls `getOrCreateChildWithName(LAYER)` repeatedly, which returns the same first child; the remaining `MTILayer` objects attach to invalid trees.
- **Evidence:** constructing `MTISample` from a fresh `CLIP` produces one `LAYER`, despite `MAX_LAYERS=4`.
- **Fix direction:** append distinct layer nodes until the count reaches four and validate each layer attachment; test save/reload of every layer.

### F5 MED: external MIDI clock is incomplete and its clock API can divide by zero

- **Where:** `engine/audio_engine/engine/mt_audio_manager.cpp:75-81`; `engine/audio_midi/source/CMidiClock.cpp:177-214,303-317`; `CMidiClock.h:104`.
- **Scenario:** MIDI Clock/Start/Stop/Continue currently reach empty handlers, so an external transport does not drive the sequencer. Independently, if the public external-clock mode is enabled, `tickExternal()` assigns `ts=te` before computing `te-ts`, recording only zero intervals; `averageOfExternalIntervals()` then divides by a zero count. The timestamp source is milliseconds despite microsecond naming.
- **Scope:** no production call to `setClockSyncMode()` or `tickExternal()` was found. Treat the crash as a dormant API defect and the current user behavior as a sync feature gap, not as a reproduced hardware crash.
- **Fix direction:** wire and test external transport as one change, fixing timestamp ordering/units and guarding an empty interval history before enabling that path.

## Additional hardware and IPC findings

### H1 HIGH: MCU HEX records can overwrite parser buffers

- **Where:** `hardware/source/kick_FirmwareUpdate.cpp:151,213-215,350-415`.
- **Scenario:** supply a data record longer than 30 bytes, or an unsupported record type whose declared data/checksum exceeds five bytes. `readNext()` has no destination-capacity argument; data records write `redData[i+2]` into the caller's 32-byte buffer, and unsupported types read `totalDataBytes*2+2` into `tempData[5]`.
- **Effect:** malformed or otherwise unsupported firmware records corrupt memory. The send loop additionally uses `i <= totalRead`, sending one byte beyond the returned count. Record checksums and read lengths are not validated before sending.
- **Fix direction:** parse complete bounded records with checked lengths/checksums, reject unsupported records safely, and send exactly the decoded byte count. Test without invoking flashing.

### H2 HIGH: firmware-version parsing writes outside its stack buffer — ASan reproduced

- **Where:** `hardware/source/kick_Hwpref.cpp:398-433`.
- **Scenario:** an `R` or `E` occurs at byte 18 or 19 in the 20-byte version header. The parser sets `fw_ver_index=i+2` and writes the terminator to index 20 or 21 of `version_buffer[20]`.
- **Evidence:** an otherwise synthetic 20-byte file with `R` at byte 18 produces a stack-buffer-overflow at line 430 on Windows; no hardware was involved.
- **Fix direction:** parse a bounded string with an explicit valid header grammar and reject a terminator outside the input. Fuzz the header parser independently of update execution.

### H3 HIGH: firmware transfer accepts short reads and can write uninitialized bytes

- **Where:** `hardware/source/kick_UsbFunctionFS.cpp:339-347,374-418,485-503`; related progress socket reader `kick_FirmwareUpdate.cpp:71-85`.
- **Scenario:** a USB payload is split across reads or the nonblocking read returns `EAGAIN`. The code reads once into `malloc(data_size)`, then writes the entire requested size even when fewer bytes arrived or the read failed. Progress is advanced before that read. Opening an existing output file omits `O_TRUNC`; close reports completion without checking the expected total.
- **Effect:** corrupt or stale-tailed images can reach validation, and the peer can receive misleading completion status. The progress IPC receiver also assumes one stream read returns a whole structure and closes on a legal short read.
- **Fix direction:** maintain bounded per-transfer state; read/write exact counts with cancellation and timeouts, track received rather than announced bytes, truncate temporary destinations and verify completion before acknowledging.

### H4 HIGH: the USB updater accepts unrestricted output paths

- **Where:** `hardware/source/kick_UsbFunctionFS.cpp:339-347,548-574`.
- **Scenario:** while the updater's USB service is active, a connected peer sends `WFile:` with an absolute path or `../` traversal. The filename goes directly to `open(O_WRONLY|O_CREAT)`.
- **Effect:** the peer can overwrite any file accessible to the updater process, beyond the intended firmware staging directory. This is physical-USB exposure during the update service, not an established Internet-facing attack path.
- **Fix direction:** choose the staging destination on the device, or canonicalize and restrict all permitted writes to a dedicated directory, reject symlink escapes and impose size limits. Handle malformed numeric sizes without uncaught `stoul` exceptions.

### H5 HIGH: UART receive handling is unsafe for fragmented or truncated frames

- **Where:** `input_events/input_events.cpp:157-194,215-226`; `contracts/dcl/dcl_contracts_helper.cpp:79-95`.
- **Scenario:** a read contains one complete frame followed by fewer than six header bytes, or a valid frame is split between callbacks. Each loop reads `byteArray[5]` without first checking the remaining header length. Incomplete frames are discarded rather than retained. The length byte is read through signed `char`, so values above 127 can become negative lengths on this toolchain.
- **Effect:** out-of-bounds reads, lost button/update messages, and potentially unbounded traversal. Contract helpers subsequently index button/pad payloads without checking their minimum lengths.
- **Fix direction:** feed a persistent byte-stream parser with explicit unsigned lengths, validate header/frame availability before indexing, and validate command-specific payload lengths once at ingress. Test every split boundary, including an otherwise-valid frame with a short tail.

### H6 HIGH: process-name lookup returns a reference to a destroyed temporary

- **Where:** `ipc/process/mt_ipc_process_manager.cpp:266-272`; caller `ipc/subsystem/mt_ipc_base_subsystem.cpp:456`.
- **Scenario:** heartbeat recovery asks for a configured child's name. `processName()` returns `const juce::String&`, but `juce::var::getProperty()` returns a var that is converted to a temporary String. That temporary is destroyed before the caller copies the reference.
- **Effect:** undefined behavior/crash during process recovery. The fallback static string is safe, which can hide the defect until a real configured process is queried.
- **Fix direction:** return String by value or return an owned stable String member. Exercise recovery as well as startup.

### H7 MED: server registration removes a registered connection using the temporary ID

- **Where:** `ipc/connection/mt_ipc_socket_server.cpp:205-225`.
- **Scenario:** a newly accepted socket has temporary connection ID 1 and registers as logical client ID 1 (or as another client whose ID collides with an existing entry). After storing `registered[clientConnectionId]`, the function unconditionally calls `registered.remove(connectionId)`.
- **Effect:** it removes the just-registered entry or another client's bookkeeping. Validation/all-connected checks and duplicate-registration handling become inconsistent, even if another owner keeps the socket alive.
- **Fix direction:** separate temporary and logical ID namespaces; after registration remove only the temporary unregistered entry. Test matching/colliding IDs and reconnects.

### H8 MED: IPC JSON messages truncate non-ASCII text — reproduced

- **Where:** `ipc/connection/mt_ipc_connection.cpp:110-112`.
- **Scenario:** publish JSON containing a non-ASCII project/program/device name. `MemoryBlock(json.getCharPointer(), json.length())` uses character count as a UTF-8 byte count.
- **Evidence:** a 15-character JSON value containing `Bass` with an umlaut needs 16 UTF-8 bytes; the exact payload construction truncates it, dropping JSON content.
- **Fix direction:** construct the payload from `toRawUTF8()` plus `getNumBytesAsUTF8()` and round-trip accented and non-Latin names through a connection.

## Additional UI findings

### P1 HIGH: asynchronous list-box setters can access destroyed controls

- **Where:** `kick_ui_controls/source/kick_listbox.cpp:551-565`; examples of dynamic clients include mixer rows and program/sample-editor controls.
- **Scenario:** `setSelected()` queues a callback capturing raw `this`, even when called on the message thread with `dontSendNotification`. Delete or rebuild the owning row/dialog before the queue drains.
- **Effect:** the queued callback calls into freed component memory. It can also apply an old selection after a newer binding has been installed.
- **Fix direction:** perform message-thread changes synchronously where appropriate, or use `Component::SafePointer` and a binding/version check for deferred work. Cover rapid create/select/destroy and rebinding.

### P2 HIGH: shared-control signal callbacks outlive their components

- **Where:** `kick_ui_controls/source/kick_CyclicStateControl.cpp:10-12`; `kick_CyclicStateControl.h:4-35`; related untracked theme slots occur throughout controls/components/mixers.
- **Scenario:** destroy a sample-editor layer containing `KickCyclicStateControl`, then emit a theme change. The signal retains an ordinary lambda capturing the component; no scoped connection or tracked owner is supplied and no destructor disconnects it.
- **Effect:** callback into freed memory. `sigslot::connection` destruction does not auto-disconnect; the library provides `scoped_connection` separately (`3rdParty/signal/signal.hpp:781-855`).
- **Fix direction:** bind connections to an owned lifetime rather than calling `disconnect_all()` on shared signals. Also audit the per-translation-unit `static themeChanged` declaration, which means subscribers in other module translation units do not share the emitted signal.

### P3 HIGH: mixer knob handlers dereference a nonexistent first row

- **Where:** `kick_ui_channel_mixer/source/kick_program_mix.cpp:79-84`; `kick_track_midi_mix.cpp:98-103`.
- **Scenario:** open the program mixer with no programs, or the MIDI mixer in a sequence with only audio tracks, and turn a linked knob. Each handler accesses `rowComp[0]` and immediately dereferences its cast result before checking row count.
- **Effect:** null dereference. Checking entries later in the switch cannot protect the initial access.
- **Fix direction:** return early for zero rows and validate the component before calculating column bounds. Include empty/mixed projects in simulator navigation coverage.

### P4 MED: MIDI mixer row numbers are confused with sequence track numbers

- **Where:** `kick_ui_channel_mixer/source/kick_track_midi_mix.cpp:37-60,170-175`.
- **Scenario:** track 0 is Audio and track 1 is MIDI. The provider displays MIDI row 0 for track 1, but its selection callback calls `setSelectedTrack(0)`; visibility similarly passes the sequence track index directly to the filtered row selector.
- **Effect:** clicking/highlighting a MIDI row selects the wrong track in the engine, affecting subsequent navigation/performance.
- **Fix direction:** map through `getMidiTrackIndexes()` in both directions; never use a filtered row index as an unfiltered track index.

### P5 MED: Edit Zones visually deselects a pad without removing it from the edit set

- **Where:** `program_edit_ui/sources/common/kick_EditZones.cpp:64-79`.
- **Scenario:** enable Multiple selection and tap a selected pad again. The highlight is toggled off, but `selected.addIfNotAlreadyThere()` is the only mutation of the selection list.
- **Effect:** bulk operations can still modify pads that the UI presents as deselected.
- **Fix direction:** remove the pad when toggling it off and derive highlighting from the same selection state returned by `getEditZones()`.

### U19 HIGH: file-delete dialog callbacks capture the local popup pointer by reference

- **Where:** `kick_ui/modes/filebrowser/kick_fileBrowser.cpp:1138-1158`.
- **Scenario:** open Delete Confirmation, let `deleteSystem()` return, then press either button. Both `popup->button*.onClick` lambdas use `[&]` and dereference the function-local `popup` variable.
- **Effect:** a dangling stack reference is used before closing or executing the destructive action. The accepted path also indexes `tempList` after refreshing a possibly empty directory.
- **Fix direction:** capture the popup by value with safe component ownership, snapshot and revalidate the selected paths, and avoid indexing the refreshed list without a bounds check. Retain U14's current-project/ancestor protection separately.

## Reconciliation of inherited findings

The inherited IDs remain stable. Detailed original locations are in the handoff; this table records the verification result and corrections rather than treating every original statement as proven.

| ID | Review result |
|---|---|
| A1 | **Reproduced.** Enabling the arp after note-on leaves one gated voice after release. Carry-over bypassing `startNote()` is also visible in `setPatch()`. |
| A2 | **Reproduced.** Two poly notes, switch to Mono, release both: one voice stays active. Stale mono held-stack handling is source-confirmed. |
| A3 | **Source-confirmed.** `InstrumentObject::prepareToPlay()` drops its sounding-note mapping without resetting/releasing the engine. Same-rate `ModularEngine::prepare()` does nothing; changed-rate graph prepare also retains arp/held state. |
| A4 | **Confirmed guard limitation.** Only the last mix sample is checked for finiteness; no finite-amplitude bound exists. Runaway/feedback output and a chosen limiter threshold still need an explicit audio stress/quality test. |
| A5 | **Source-confirmed realtime cost.** Every released voice resets each full Delay vector. About 192 KB per module per voice at 48 kHz; roughly 3 MB across 16 voices. The claim is memory work, not a measured underrun. |
| A6 | **Source-confirmed formula; inherited behavior verified.** The same `filtAdsr*depth*((lfoToFilter*lfoOut)+(lfoToPitch*lfoOut))` appears in initial public commit `04ee49e4`. A filter envelope with both LFO amounts zero contributes zero modulation. Correct the formula with intended sound behavior specified. Filter coefficient transcendental work is a separate profiling candidate; current `tan` location is `compat/AudIO_juce/source/mt_voice_dsp.h:175`. |
| A7 | **Confirmed structural discontinuity; sonic impact unmeasured.** Graph replacement preserves gated notes only and discards release voices/effect instances. Crossfades/tail preservation are an improvement, not an already-tested repair. |
| A8 | **Source-confirmed.** Assigning a fresh fractional step discards negative countdown overshoot. Test long-run timing at tempos whose step length is fractional in samples. |
| A9 | **Source-confirmed.** NoteOn/NoteOff/NoteOn for one input within a block leaves the earlier deferred off targeting the retrigger next block. |
| A10 | **Source-confirmed.** Short sub-blocks overwrite only the first N samples of a cached 16-sample cable output; a subsequent longer delayed read can use the prior tail. |
| A11 | **Source-confirmed rare contention path.** A sample-rate change during graph build causes prepare/allocation under the graph spin lock. No contention benchmark was run. |
| A12 | **Confirmed optimization candidates only.** Per-sample sine/tanh/tan calls exist. No profile establishes that replacing them is worthwhile, and approximations need audio-quality tests. |
| A13 | **Confirmed feature gap.** Instrument MIDI handling forwards CC1 but has no CC64 sustain behavior. |
| A14 | **Source-confirmed.** Different preset names can sanitize to the same filename. `remove()` also lacks a patch tag check, though the directory is dedicated to Modular presets; collision loss is the material issue. |
| U1 | **Source-confirmed.** Seven `parameterMapping()` asynchronous lambdas capture locals by reference. Merely copying the raw program pointer would still leave a separate lifetime risk; re-resolve stable program/bus IDs on execution. |
| U2 | **Source-confirmed routing/deadline defect; hardware not exercised.** `KickHome` handles SYSTEM messages in a branch that does not forward SW_UPDATE to the screen. The MCU idle loop has neither sleep nor timeout. Physical update completion/bricking was not tested. |
| U3 | **Source-confirmed.** Worker callbacks mutate component/string state, and completion-dialog callbacks retain local popup-pointer references. |
| U4 | **Source-confirmed.** `deviceData.clear()` follows row population; a later Update callback indexes that cleared vector. Setting `onThreadEnd` after `startThread()` is a separate race. |
| U5 | **Source-confirmed.** Global hardware navigation remains active and making the update screen visible starts another detached worker without a shared in-progress guard. |
| U6 | **Source-confirmed command.** The post-update path invokes `reboot -nf`. Replace with a controlled sync/reboot flow; actual device/filesystem behavior was not exercised. |
| U7 | **Source-confirmed.** Template copy creates/uses the requested folder and copies over a matching destination without collision/name/path validation. The separate new-project check also only consults recent entries, not every existing directory. |
| U8 | **Source-confirmed, shared-control cause located.** `kick_ui_controls/source/kick_table.cpp:363-370` grows but never trims owned rows. AppTrack iterates stale rows and writes through their former `pTrack`. Mixer metering shares this lifetime risk. |
| U9 | **Source-confirmed bounds defects.** `knobClicked()` checks only `selectedIndex>=0`; visibility also accesses the chosen item unconditionally. Test removal and an empty asset/project installation. |
| U10 | **Confirmed loader-thread ValueTree edits; narrow the race claim.** Project-loaded listeners execute synchronously on the loading thread and RecentProject updates preferences there. The non-atomic `isProjectLoading` alone does not prove a race: the two located assignments are UI start and a delayed message-thread callback. |
| U11 | **Source-confirmed performance/overflow issue.** Folder sizing recursively enumerates all descendants and recursively enumerates directories again, discarding those recursive return values; UI rows repeat the work. The result narrows from int64 to int32. |
| U12 | **Source-confirmed.** JUCE's thumbnail overload expects a sample rate; the caller supplies the buffer's sample count and repeatedly rebuilds it while recording. No UI timing benchmark was run. |
| U13 | **Source-confirmed.** Header Back closes the view without stopping USB or clearing its callback; the callback can later navigate back into update flow. The singleton view's mere hiding is not itself proof of freed memory. |
| U14 | **Source-confirmed.** Name equality does not protect a current-project ancestor from recursive deletion; unrelated matching names can also be falsely blocked. Use canonical path ancestry, not basenames. |
| U15 | **Source-confirmed with scope limits.** The one-channel branch reads element 1, but normal stereo configuration does not enter it. The reverse-state line updates `playB`, then immediately overwrites it with the play state, leaving `reverseB` unsynchronized. |
| U16 | **Confirmed feature gaps.** Sampler Clear/Export and Song Export handlers do not implement their named operation. |
| U17 | **Source-confirmed missing guard.** Saving/assigning a recording dereferences the currently selected program without validating it. Test zero programs or deletion/selection changes before dialog completion. |
| U18 | **Source-confirmed.** Several content allocations are deleted only on Apply, leaking on Cancel. The update-status expression ORs in the nonzero `IMAGE_EXTRACTION_E` enum directly, making that condition true. |

## Rejected suspicion and remaining limitations

- **Effect removal resurrecting deleted effects was rejected.** `AudIO/AudIO/Daw/DawInserts.cpp:77-99` uses four fixed slots. Removing an effect resets its slot, and serialization writes all four names, including empty names. The probe's `runtime=4 saved-tree=4` represents slot count, not four live effects. Per-effect bypass is also included in `serializeEffect()`; it is not missing from serialization.
- **Some APIs still merit bounded follow-up:** `CV::setCentVoltage()` indexes a 61-entry calibration array with an absolute note index instead of the validated note relative to the channel root (`hardware/source/kick_I2c.cpp:128-138`); no caller was found in this checkout. `KickCyclicStateControl::setNormalized(1.0)` can index one past its state list (`kick_ui_controls/source/kick_CyclicStateControl.cpp:23-32`). These were not promoted to high-priority user-path findings without an active caller/reproduction.
- Multiprocess recovery also has unsynchronized process/connection maps and asynchronous callbacks retaining raw owners; a Linux integration stress run is needed to isolate the full shutdown/reconnect failure set. `apps/kick_main/kick_heart_beat_data.h:77` uses a timestamp value as `hostId`; its current callback restarts the entire subsystem and ignores that ID, so a wrong-target restart was not claimed.
- Baseline tests and probes do not establish deadline compliance on ARM hardware. Profile callback duration before undertaking A12 approximations or broader DSP optimization.

## Recommended repair order

1. **Memory safety and data protection:** U1/U19, E3/E11, F2, H1/H2, U7/U14 and E7. Add narrow reproductions as permanent tests while fixing. Handle the shared stale-row problem U8 together with mixer lifetimes.
2. **Hardware updater as a coherent batch:** U2-U6/U13/U18 plus H3-H5. Require bounded/cancellable transfer state and test routing/parsers/short reads without flashing first; hardware acceptance remains necessary.
3. **Playback and persistence correctness:** E1/E2/E4-E6, F1/F3/F4, A1-A3/A9/A10 and P1/P3/P4. Include note-edit undo, live editing, first-tick scheduling, MIDI hotplug and project round trips.
4. **Remaining API/UI gaps:** E8-E10, F5, H6-H8, P2/P5, U9-U12/U15-U17 and preset collision handling. Re-run affected scenarios and the baseline suites for each batch.
5. **Optimization after correctness:** benchmark A5/U11/U12 first; measure A11 contention and A12 DSP costs before changing algorithms. Treat A7 tail preservation and A13 sustain as explicit behavior additions.

Keep the original publishing rules: only push to `fork` when asked, never to `origin` or upstream PRs, never include ASIO SDK/builds or personal preferences/commercial kits, and build distributable archives only from `Kick/build/dist`.
