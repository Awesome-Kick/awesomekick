// Regression coverage for AUDIT-REVIEW.md. Synthetic data only; never flashes hardware.
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "hardware/hardware.h"
#include "input_events/input_events.h"

namespace mt
{
    struct TrackAuditAccess
    {
        static void route(MTTrack& track, MTIProgram* program) { track.program.store(program); }
    };
}

namespace
{
    struct AuditProgram : mt::MTIProgram
    {
        using MTIProgram::MTIProgram;
        juce::Array<juce::MidiMessage> received;
        void noteProcess(juce::Array<juce::MidiMessage>& messages) override { received.addArray(messages); }
    };

    struct FailingEditStream : juce::OutputStream
    {
        void flush() override {}
        juce::int64 getPosition() override { return 0; }
        bool setPosition(juce::int64) override { return true; }
        bool write(const void*, size_t) override { return false; }
    };
    struct FailingReverseEdit : mt::MTSampleEditorReverseAction
    {
        std::unique_ptr<juce::OutputStream> createEditOutputStream(const juce::File&) override { return std::make_unique<FailingEditStream>(); }
    };

    void testAuditRegressions(Results& R)
    {
        pump(500); // Let the engine publish its deferred audio-device initialization.
        R.section("Audit: MIDI batches, firmware and UART boundaries");
        {
            mt::MTMidiDeviceManager midi;
            juce::StringArray batches;
            midi.onMidiMessages = [&](juce::Array<mt::MTMidiMessage>& messages) {
                juce::StringArray notes;
                for(const auto& message : messages) notes.add(juce::String(message.message.getNoteNumber()));
                batches.add(notes.joinIntoString(","));
            };
            // Exercise the real private JUCE callback interfaces, with no MIDI hardware.
            for(int note : {60, 61, 62})
            {
                ((juce::MidiInputCallback*)&midi)->handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOn(1, note, (juce::uint8)100));
                ((juce::AsyncUpdater*)&midi)->handleUpdateNowIfNeeded();
            }
            R.check(batches.joinIntoString("|") == "60|61|62", "F1: each MIDI batch is delivered exactly once");
        }
        const auto scratch = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("KickAudit-" + juce::Uuid().toString());
        scratch.createDirectory();
        const auto hexFile = scratch.getChildFile("parser.hex");
        auto& parser = mt::FirmwareUpdate::getSwupdateInstance().parser;
        auto record = [](unsigned type, std::vector<unsigned> payload) {
            std::vector<unsigned> bytes{(unsigned)payload.size(), 0, 0, type};
            bytes.insert(bytes.end(), payload.begin(), payload.end());
            unsigned checksum = 0;
            juce::String text(":");
            for(auto byte : bytes) { checksum += byte; text += juce::String::formatted("%02X", byte); }
            return text + juce::String::formatted("%02X\n", (256u - (checksum & 255)) & 255);
        };
        char parsed[32]{};
        hexFile.replaceWithText(record(4, {8, 0}) + record(0, {1, 2, 3}) + record(1, {}));
        parser.AddPath(hexFile.getFullPathName());
        const bool validHex = parser.readNext(parsed) == -2 && parser.readNext(parsed) == 5 && parsed[2] == 1 && parsed[4] == 3 && parser.readNext(parsed) == 0;
        R.check(validHex, "H1: valid address, data and EOF records round-trip");
        hexFile.replaceWithText(record(0, std::vector<unsigned>(255, 1)));
        parser.AddPath(hexFile.getFullPathName());
        R.check(parser.readNext(parsed) == -3, "H1: oversized firmware data is rejected before copying");
        hexFile.replaceWithText(":0100000001FF\n"); // Deliberately incorrect checksum.
        parser.AddPath(hexFile.getFullPathName());
        R.check(parser.readNext(parsed) == -3, "H1: invalid firmware checksum is rejected");
        hexFile.replaceWithText(":030000000102\n");
        parser.AddPath(hexFile.getFullPathName());
        R.check(parser.readNext(parsed) == -3, "H1: truncated firmware record is rejected");
        parser.AddPath(scratch.getChildFile("missing.hex").getFullPathName());
        R.check(parser.readNext(parsed) == -3, "H1: unreadable firmware is rejected");
        bool headersSafe = true;
        for(int marker : {18, 19})
        {
            std::array<char, 20> bytes{};
            bytes.fill('0'); bytes[(size_t)marker] = 'R';
            const auto image = scratch.getChildFile("version.img");
            image.replaceWithData(bytes.data(), bytes.size());
            mt::linuxFwInfo version{};
            headersSafe &= !mt::HwPref::getHwPref().getVersionfromFile(image.getFullPathName().toStdString(), version);
        }
        R.check(headersSafe, "H2: version markers at both header edges are rejected safely");
        bool everySplit = true;
        const std::vector<unsigned char> frame{'M', 'T', 0, 0, 0, 4, 1, 2, 3, 4, '\n'};
        for(size_t split = 0; split < frame.size(); ++split)
        {
            Controls::SerialFrameBuffer stream;
            std::vector<unsigned char> result;
            stream.append(frame.data(), split);
            everySplit &= !stream.pop(result);
            stream.append(frame.data() + split, frame.size() - split);
            everySplit &= stream.pop(result) && result == frame && !stream.pop(result);
        }
        R.check(everySplit, "H5: UART frame survives every header and payload split");
        {
            Controls::SerialFrameBuffer stream;
            std::vector<unsigned char> result;
            auto longFrame = std::vector<unsigned char>(262, 0);
            longFrame[0] = 'M'; longFrame[1] = 'T'; longFrame[5] = 255;
            stream.append(longFrame.data(), longFrame.size());
            stream.append(frame.data(), 3);
            const bool first = stream.pop(result) && result == longFrame && !stream.pop(result);
            stream.append(frame.data() + 3, frame.size() - 3);
            R.check(first && stream.pop(result) && result == frame, "H5: unsigned maximum payload and a short next header are preserved");
        }

        R.section("Audit: slice persistence and reverse rendering");
        const auto wav = scratch.getChildFile("ramp.wav");
        {
            juce::WavAudioFormat format;
            auto stream = wav.createOutputStream();
            std::unique_ptr<juce::AudioFormatWriter> writer(format.createWriterFor(stream.release(), 48000, 2, 16, {}, 0));
            juce::AudioBuffer<float> ramp(2, 128);
            for(int channel = 0; channel < 2; ++channel)
                for(int sample = 0; sample < 128; ++sample) ramp.setSample(channel, sample, sample / 256.f);
            R.check(writer && writer->writeFromAudioSampleBuffer(ramp, 0, 128), "synthetic slice fixture writes successfully");
        }
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        mt::ASound::Ptr sound = new mt::ASound(wav.getFullPathName(), formats, 1.f, -1);
        juce::ValueTree sliceTree(mt::IDs::SLICE);
        sliceTree.setProperty(mt::IDs::loopStart, 12, nullptr);
        sliceTree.setProperty(mt::IDs::loopEnd, 80, nullptr);
        mt::ASoundSlice slice(sliceTree);
        slice.setSource(sound);
        R.check(slice.getStart() == 12 && slice.getEnd() == 80, "F3: attaching a restored source preserves trim boundaries");
        slice.setReverse(true);
        mt::ASliceRenderer renderer;
        renderer.setSlice(&slice);
        bool reverseExact = true;
        for(int sample = 79; sample >= 12; --sample)
            reverseExact &= std::abs(renderer.getNext(1.0).getSample(0, 0) - sound->getData().getSample(0, sample)) < 0.0001f;
        R.check(reverseExact && renderer.hasFinished(), "F2: reverse playback visits every trimmed sample once and finishes");
        for(int i = 0; i < 512; ++i) renderer.getNext(1.0);
        slice.setLooping(true);
        renderer.reset();
        bool wrapped = true;
        for(int i = 0; i < 100; ++i)
        {
            renderer.getNext(1000.25);
            wrapped &= renderer.getPosition() >= 12 && renderer.getPosition() < 80;
        }
        R.check(wrapped && renderer.isRunning(), "F2: reverse loop wraps pitch steps larger than its length");
        slice.setReverse(false);
        slice.setLooping(false);
        renderer.reset();
        for(int i = 0; i < 272; ++i) renderer.getNext(0.25);
        R.check(renderer.hasFinished(), "F2: fractional forward playback crosses the final sample instead of sticking");
        slice.setLoopPoints(12, 12);
        renderer.reset();
        R.check(renderer.hasFinished() && renderer.getNext().getMagnitude(0, 1) == 0.f, "F2: empty slice renders silence");
        juce::ValueTree clip(mt::IDs::CLIP);
        mt::MTISample sample(clip, nullptr);
        R.check(clip.getChildWithName(mt::IDs::LAYERS).getNumChildren() == 4, "F4: new sample owns four distinct persistent layers");

        R.section("Audit: failed edits and preset collisions preserve data");
        juce::MemoryBlock original;
        wav.loadFileAsData(original);
        auto editable = wav;
        FailingReverseEdit failing;
        failing.initialize(mt::MTSampleRegion(0, 128), {}, editable);
        const bool failedWrite = !failing.execute();
        juce::MemoryBlock afterFailure;
        wav.loadFileAsData(afterFailure);
        R.check(failedWrite && afterFailure == original, "E7: failed audio writes leave the original byte-for-byte intact");
        {
            juce::UndoManager history;
            auto action = std::make_unique<mt::MTSampleEditorReverseAction>();
            action->initialize(mt::MTSampleRegion(0, 128), {}, editable);
            const bool performed = history.perform(new mt::MTSampleEditUndoAction(action.release()));
            history.beginNewTransaction();
            const bool undone = history.undo();
            juce::MemoryBlock restored;
            wav.loadFileAsData(restored);
            const bool redone = history.redo();
            R.check(performed && undone && restored == original && redone, "E7: destructive edit undo restores exact bytes and redo succeeds");
            history.undo();
        }
        const auto corrupt = scratch.getChildFile("corrupt.wav");
        corrupt.replaceWithText("not a wave file");
        auto corruptSource = corrupt;
        mt::MTSampleEditorReverseAction invalidEdit;
        invalidEdit.initialize(mt::MTSampleRegion(0, 128), {}, corruptSource);
        R.check(!invalidEdit.execute() && corrupt.loadFileAsString() == "not a wave file", "E7: unreadable edit source is rejected intact");
        {
            mt::MTSamplePlayer preview;
            preview.setSource(corrupt);
            preview.start();
            R.check(!preview.isPlaying(), "E8: previewing unreadable audio stays stopped");
        }
        {
            const auto previous = mt::modular::UserPresets::getDirectory();
            const auto presets = scratch.getChildFile("Presets");
            mt::modular::UserPresets::setDirectory(presets);
            const auto patch = mt::modular::createPreset(0);
            const bool saved = mt::modular::UserPresets::save(patch, "Audit?") && mt::modular::UserPresets::save(patch, "Audit");
            R.check(saved && mt::modular::UserPresets::load("Audit?").isValid() && mt::modular::UserPresets::load("Audit").isValid(), "A14: names with colliding legal filenames both survive");
            const auto unrelated = presets.getChildFile("unrelated.xml");
            unrelated.replaceWithText("<OTHER name=\"Keep\"/>");
            R.check(!mt::modular::UserPresets::remove("Keep") && unrelated.existsAsFile(), "A14: preset removal checks the patch tag");
            mt::modular::UserPresets::setDirectory(previous);
        }

        R.section("Audit: live note index, routing, transport and lifetime");
        auto& ai = mt::getAudioInterface();
        auto project = scratch.getChildFile("Project");
        project.createDirectory();
        ai.loadProject(project);
        const int sequence = ai.createSequence("Audit");
        const int trackIndex = ai.createTrack(sequence, "Audit Track");
        auto* track = ai.getManager()->getTrack(sequence, trackIndex);
        track->setUsingSeqLength(true);
        auto& undo = ai.getUndoManager();
        undo.clearUndoHistory(); undo.beginNewTransaction();
        track->addOrModifyNote(60, 96, 48);
        undo.beginNewTransaction(); undo.undo();
        R.check(track->getNumNotes() == 0 && track->getNotesAtPos(96).empty(), "E1: undo-add removes the playback note");
        undo.redo(); undo.clearUndoHistory(); undo.beginNewTransaction();
        track->eraseNote(60, 96, 48);
        undo.beginNewTransaction(); undo.undo();
        R.check(track->getNumNotes() == 1 && track->getNotesAtPos(96).size() == 1, "E1: undo-delete restores the playback note");
        auto treeNote = track->getNoteListVT().getChild(0);
        treeNote.setProperty(mt::IDs::start, 192, nullptr);
        R.check(track->getNotesAtPos(96).empty() && track->getNotesAtPos(192).size() == 1, "E1: direct tree edits move the playback bucket");
        std::atomic<bool> done{false}, snapshotsValid{true};
        std::thread playback([&] {
            while(!done.load())
                for(const auto& note : track->getNotesAtPos(192))
                    if(note.number != 60 || note.length < 1) snapshotsValid.store(false);
        });
        for(int i = 0; i < 500; ++i) { track->deleteAllNotes(); track->addOrModifyNote(60, 192, 48); }
        done.store(true); playback.join();
        R.check(snapshotsValid.load(), "E2/E9: concurrent snapshots survive repeated clear and replacement");
        track->setMidiPort("AUDIT_PORT");
        R.check(track->getMidiPort() == "AUDIT_PORT", "E10: MIDI-port setter round-trips its argument");
        {
            juce::ValueTree programTree(mt::IDs::PROGRAM);
            AuditProgram capture(programTree);
            mt::TrackAuditAccess::route(*track, &capture);
            track->deleteAllNotes();
            track->setTranspose(0); track->setMidiOutChannel(2);
            track->processIncomingMidiMessage(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100));
            track->setTranspose(12); track->setMidiOutChannel(5);
            track->processIncomingMidiMessage(juce::MidiMessage::noteOff(1, 60));
            R.check(capture.received.size() == 2 && capture.received[0].getNoteNumber() == 60 && capture.received[1].getNoteNumber() == 60 && capture.received[1].getChannel() == 2, "E4: release retains note-on destination after transpose and channel changes");
            capture.received.clear();
            track->setTranspose(0); track->setMidiOutChannel(1);
            track->addOrModifyNote(60, 0, 96, 100, 100, 4);
            track->play(); track->onTick(1);
            R.check(capture.received.size() == 1 && capture.received[0].isNoteOn(), "E5: the first tick schedules the note at zero");
            capture.received.clear(); track->onTick(2);
            R.check(capture.received.isEmpty(), "E6: first ratchet is not released on the next tick");
            track->onTick(23);
            R.check(capture.received.size() == 1 && capture.received[0].isNoteOff(), "E6: first ratchet release reaches its subdivision boundary");
            track->stop();
            capture.received.clear();
            track->play(); track->onTick(100);
            bool ordered = capture.received.size() == 8;
            for(int i = 0; i < capture.received.size(); ++i) ordered &= (i % 2 == 0 ? capture.received[i].isNoteOn() : capture.received[i].isNoteOff());
            R.check(ordered, "E6: a delayed tick batch preserves all ratchet on/off pairs in order");
            track->stop();
            mt::TrackAuditAccess::route(*track, nullptr);
        }
        const int midiProgram = ai.createProgram(mt::MTProgramType::Midi, "Export MIDI");
        track->setProgram(ai.getProgram(midiProgram));
        track->deleteAllNotes();
        track->addOrModifyNote(60, 0, 48);
        ai.createSong("Audit Song");
        mt::MTSongStepInfo step;
        step.seqNo = sequence; step.repeatCount = 2;
        ai.addSongStep("Audit Song", step, -1);
        const auto midiFile = scratch.getChildFile("Export.mid");
        const bool exported = ai.exportSongMidi("Audit Song", midiFile);
        juce::MidiFile roundtrip;
        auto midiInput = midiFile.createInputStream();
        const bool parsedMidi = midiInput && roundtrip.readFrom(*midiInput);
        int noteOns = 0;
        if(parsedMidi)
            for(int t = 0; t < roundtrip.getNumTracks(); ++t)
                for(int event = 0; event < roundtrip.getTrack(t)->getNumEvents(); ++event)
                    if(roundtrip.getTrack(t)->getEventPointer(event)->message.isNoteOn()) ++noteOns;
        R.check(exported && parsedMidi && noteOns == 2, "U16: song MIDI export preserves two sequence repeats and note events", "saved=" + juce::String(exported ? 1 : 0) + ", parsed=" + juce::String(parsedMidi ? 1 : 0) + ", note-ons=" + juce::String(noteOns) + ", track ticks=" + juce::String(track->getLengthInTicks()));
        midiInput.reset();
        const auto programIndex = ai.createProgram(mt::MTProgramType::Drum, "Lifetime");
        track->setProgram(ai.getProgram(programIndex));
        ai.deleteTrack(sequence, trackIndex);
        ai.deleteProgram(programIndex);
        R.check(true, "E11: program deletion after track deletion remains safe");
        ai.getManager()->play(sequence);
        ai.deleteSequence(sequence);
        ai.getManager()->stop();
        R.check(ai.getManager()->getPlayingSequence() == nullptr, "E3: deleting a playing sequence detaches transport before destruction");
        bool graphEditsValid = true;
        const int previousPrograms = ai.getNumPrograms();
        for(int i = 0; i < 48; ++i)
        {
            graphEditsValid &= ai.createProgram(mt::MTProgramType::Drum, "Graph stress " + juce::String(i)) >= 0;
            if(i % 4 == 0) pump(2); // Keep the real audio callback running across bus growth.
        }
        R.check(graphEditsValid && ai.getNumPrograms() == previousPrograms + 48, "H9: live bus growth remains safe while the audio graph renders");
        ai.closeProject();

        R.section("Audit: Modular transitions and external clock");
        auto drain = [](mt::ModularEngine& engine) {
            float left[128]{}, right[128]{}; float* output[]{left, right};
            for(int i = 0; i < 1600; ++i) engine.process(output, 2, 128);
        };
        for(bool changeMode : {false, true})
        {
            auto patch = mt::modular::createPreset(0);
            mt::modular::PatchEditor edit(patch, nullptr);
            mt::ModularEngine engine;
            engine.prepare(48000, 128); engine.setPatch(patch);
            engine.noteOn(60, 1.f); engine.noteOn(64, 1.f);
            if(changeMode) edit.setPlayMode(mt::modular::Mono); else edit.setArp(mt::modular::ArpUp);
            engine.setPatch(patch);
            engine.noteOff(60); engine.noteOff(64); drain(engine);
            R.check(engine.getNumActiveVoices() == 0, changeMode ? "A2: changing poly to mono leaves no held voices" : "A1: enabling arp with keys down leaves no held voices");
            engine.noteOn(60, 1.f); engine.prepare(48000, 128); drain(engine);
            R.check(engine.getNumActiveVoices() == 0, "A3: same-rate prepare resets held notes and arp");
        }
        {
            mt::modular::PatchBuilder patch("Audit profile", 16);
            patch.add("osc", 0, 0, {{"wave", 0}, {"level", 0.25f}, {"unison", 4}});
            patch.add("filter", 0, 1, {{"mode", 4}, {"cutoff", 1000}, {"reso", 0.4f}});
            patch.add("drive", 0, 2, {{"drive", 0.4f}});
            patch.add("delay", 0, 3, {{"time", 30}, {"mix", 0.2f}});
            patch.addEffect("chorus", 0, {{"mix", 0.2f}});
            mt::ModularEngine engine;
            engine.prepare(48000, 128); engine.setPatch(patch.tree);
            for(int note = 48; note < 64; ++note) engine.noteOn(note, 0.8f);
            float left[128]{}, right[128]{}; float* output[]{left, right};
            const auto begin = juce::Time::getHighResolutionTicks();
            for(int block = 0; block < 375; ++block) engine.process(output, 2, 128);
            const auto elapsed = juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - begin);
            R.note("A12 profiling: one second of 16-voice osc/ladder/drive/delay plus chorus took " + juce::String(elapsed * 1000.0, 2) + " ms in this desktop build; not an ARM deadline measurement.");
        }
        auto& clock = mt::getMidiClock();
        const auto savedTick = clock.onTick;
        uint32_t lastTick = 0;
        clock.setClockSyncMode(mt::CMidiClock::SyncMode::EXTERNAL_CLOCK);
        R.check(std::isfinite(clock.getTempo()), "F5: empty external interval history is safe");
        clock.onTick = [&](uint32_t tick) { lastTick = tick; };
        clock.start(true);
        for(int i = 0; i < 24; ++i) clock.tickExternal(1000.0 + i * (60000.0 / (120.0 * 24)));
        R.check(lastTick == (uint32_t)clock.getInternalPPQN(), "F5: 24 external clocks advance exactly one quarter note");
        R.check(std::abs(clock.getTempo() - 120.f) < 0.1f, "F5: batched clock delivery uses reception timestamps and measures 120 BPM");
        clock.stop(); clock.onTick = savedTick;
        clock.setClockSyncMode(mt::CMidiClock::SyncMode::INTERNAL_CLOCK);
        const auto oldTempo = clock.getTempo();
        clock.setTempo(0.f); clock.setTempo(std::numeric_limits<float>::quiet_NaN());
        R.check(std::isfinite(clock.getTempo()) && clock.getTempo() == oldTempo, "F5: zero and nonfinite tempo requests preserve a valid clock");

        // Release open fixture readers before removing their disposable directory.
        parser.AddPath(scratch.getChildFile("missing.hex").getFullPathName());
        sound = nullptr;
        scratch.deleteRecursively();
    }
}
