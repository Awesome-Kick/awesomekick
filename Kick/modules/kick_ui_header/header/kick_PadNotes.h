/*
 * Turns hardware pad presses into notes for the selected track.
 *
 * Instrument (Plugin) programs use the program's PadLayout (scale, root,
 * 4ths/3rds/sequential) with the bank buttons selecting the octave. Other
 * programs keep the original mapping: pad + bank offset.
 *
 * The note each pad started is remembered, so a release always stops the note
 * its press started, even if the bank or layout changed in between, or the
 * press and release are handled by different screens.
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#pragma once

namespace kick_ui
{
    struct PadNotes
    {
        /// Handles a pad message: sends note on/off to the selected track.
        /// Returns the note played or released, or -1 if none.
        static int handle( CPktMgr::HWMsg_t& Msg )
        {
            auto&      ae       = mt::getAudioInterface();
            const auto pad      = contracts::DclContractsHelper::getPadNumber( Msg );
            const auto state    = contracts::DclContractsHelper::getPadState( Msg );
            const auto velocity = contracts::DclContractsHelper::getPadVelocity( Msg );
            auto&      held     = heldNotes();

            if( !juce::isPositiveAndBelow( pad, (int)held.size() ) )
                return -1;

            if( state == contracts::DeviceControlContracts::BTN_PRESSED )
            {
                const auto note = noteForPad( pad );
                if( note < 0 )
                    return -1;
                if( held[(size_t)pad] >= 0 ) // re-press without a release
                    ae.addLiveMidiMsg( ae.getSelected(), juce::MidiMessage::noteOff( 1, held[(size_t)pad] ) );
                held[(size_t)pad] = note;
                ae.addLiveMidiMsg( ae.getSelected(), juce::MidiMessage::noteOn( 1, note, (juce::uint8)velocity ) );
                return note;
            }

            if( state == contracts::DeviceControlContracts::BTN_RELEASED )
            {
                const auto note   = held[(size_t)pad] >= 0 ? held[(size_t)pad] : noteForPad( pad );
                held[(size_t)pad] = -1;
                if( note >= 0 )
                    ae.addLiveMidiMsg( ae.getSelected(), juce::MidiMessage::noteOff( 1, note ) );
                return note;
            }
            return -1;
        }

        /// The note a pad plays right now for the selected track, or -1.
        static int noteForPad( int Pad )
        {
            auto&       ae   = mt::getAudioInterface();
            const auto& sel  = ae.getSelected();
            auto*       prog = ae.getProgramDataForTrack( sel.sequence, sel.track );

            if( prog && prog->type.get() == mt::MTProgramType::Plugin )
            {
                const auto layout = mt::PadLayout::fromTree( prog->programState.getChildWithName( mt::IDs::PAD_LAYOUT ) );
                return layout.noteFor( Pad, ae.getBankOffset() / 16 );
            }

            const auto note = Pad + ae.getBankOffset();
            return juce::isPositiveAndBelow( note, 128 ) ? note : -1;
        }

    private:
        static std::array<int, 16>& heldNotes()
        {
            static std::array<int, 16> notes = []() {
                std::array<int, 16> a{};
                a.fill( -1 );
                return a;
            }();
            return notes;
        }
    };
} // namespace kick_ui
