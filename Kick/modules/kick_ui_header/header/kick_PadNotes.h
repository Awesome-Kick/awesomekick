/*
 * Turns hardware pad presses into notes for the selected track.
 *
 * Instrument (Plugin) programs use the program's PadLayout (scale, root,
 * 4ths/3rds/sequential) with the bank buttons selecting the octave. Other
 * programs keep the original mapping: pad + bank offset.
 *
 * Each pad remembers the note it started and the track it went to, so a release
 * always stops exactly that note: even if the bank, layout or selected track
 * changed in between, or the press and release are handled by different screens.
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
            const auto pad   = contracts::DclContractsHelper::getPadNumber( Msg );
            const auto state = contracts::DclContractsHelper::getPadState( Msg );

            if( state == contracts::DeviceControlContracts::BTN_PRESSED )
                return press( pad, contracts::DclContractsHelper::getPadVelocity( Msg ) );
            if( state == contracts::DeviceControlContracts::BTN_RELEASED )
                return release( pad );
            return -1;
        }

        static int press( int Pad, int Velocity )
        {
            auto& held = heldNotes();
            if( !juce::isPositiveAndBelow( Pad, (int)held.size() ) )
                return -1;

            const auto note = noteForPad( Pad );
            if( note < 0 )
                return -1;

            release( Pad ); // re-press without a release
            auto& ae  = mt::getAudioInterface();
            held[(size_t)Pad] = { note, ae.getSelected() };
            ae.addLiveMidiMsg( ae.getSelected(), juce::MidiMessage::noteOn( 1, note, (juce::uint8)juce::jlimit( 1, 127, Velocity ) ) );
            return note;
        }

        static int release( int Pad )
        {
            auto& held = heldNotes();
            if( !juce::isPositiveAndBelow( Pad, (int)held.size() ) )
                return -1;

            auto h            = held[(size_t)Pad];
            held[(size_t)Pad] = {};
            if( h.note < 0 )
                return -1;
            mt::getAudioInterface().addLiveMidiMsg( h.target, juce::MidiMessage::noteOff( 1, h.note ) );
            return h.note;
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
        struct Held
        {
            int             note{ -1 };
            mt::MTSelected  target{};
        };

        static std::array<Held, 16>& heldNotes()
        {
            static std::array<Held, 16> notes{};
            return notes;
        }
    };
} // namespace kick_ui
