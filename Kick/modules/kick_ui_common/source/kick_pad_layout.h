/*
 * Push/Launchpad-style note layouts for the 4x4 pads, used by instrument
 * (Plugin) programs. Pure functions of the settings, so they can be tested and
 * shared by every screen that turns pad presses into notes.
 *
 * Pads are numbered 0..15 from the bottom-left, row by row. The bank buttons
 * A..H pick the octave: bank A starts at C0 (MIDI 24), bank D at C3 (MIDI 60).
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#pragma once

namespace mt
{
    struct PadLayout
    {
        enum class Mode
        {
            Sequential, // rows continue: 4 steps per row
            Fourths,    // Push default: in key a row is 3 scale degrees up, chromatic 5 semitones
            Thirds      // in key 2 scale degrees, chromatic 4 semitones
        };

        static constexpr int columns = 4;
        static constexpr int rows    = 4;

        Mode mode{ Mode::Fourths };
        bool inKey{ true };
        int  root{ 0 };       // 0 = C .. 11 = B
        int  scaleIndex{ 0 }; // index into ScaleAndNotes::scaleNames

        static PadLayout fromTree( const juce::ValueTree& Tree )
        {
            PadLayout l;
            l.mode       = (Mode)juce::jlimit( 0, 2, (int)Tree.getProperty( "layoutMode", (int)Mode::Fourths ) );
            l.inKey      = Tree.getProperty( "inKey", true );
            l.root       = juce::jlimit( 0, 11, (int)Tree.getProperty( "rootNote", 0 ) );
            l.scaleIndex = juce::jlimit( 0, (int)ScaleAndNotes::scaleNames.size() - 1, (int)Tree.getProperty( "scale", 0 ) );
            return l;
        }

        void toTree( juce::ValueTree Tree, juce::UndoManager* UM = nullptr ) const
        {
            Tree.setProperty( "layoutMode", (int)mode, UM );
            Tree.setProperty( "inKey", inKey, UM );
            Tree.setProperty( "rootNote", root, UM );
            Tree.setProperty( "scale", scaleIndex, UM );
        }

        /// Semitone offsets of the scale from its root, e.g. major = 0 2 4 5 7 9 11.
        std::vector<int> scaleSteps() const
        {
            std::vector<int> steps{ 0 };
            auto             it = ScaleAndNotes::scaleFormulaMap.find( (ScaleAndNotes::scale_type)scaleIndex );
            if( it == ScaleAndNotes::scaleFormulaMap.end() )
                return { 0, 2, 4, 5, 7, 9, 11 };
            for( size_t i = 0; i + 1 < it->second.size(); ++i )
                steps.push_back( steps.back() + it->second[i] );
            return steps;
        }

        /// MIDI note for a pad in a bank, or -1 if it falls outside 0..127.
        int noteFor( int Pad, int Bank ) const
        {
            if( !juce::isPositiveAndBelow( Pad, rows * columns ) )
                return -1;

            const int row  = Pad / columns;
            const int col  = Pad % columns;
            const int base = 12 * ( juce::jlimit( 0, 7, Bank ) + 2 ) + root;

            int note;
            if( inKey )
            {
                const int  rowStep = mode == Mode::Fourths ? 3 : ( mode == Mode::Thirds ? 2 : columns );
                const auto steps   = scaleSteps();
                const int  degree  = row * rowStep + col;
                const int  n       = (int)steps.size();
                note               = base + 12 * ( degree / n ) + steps[(size_t)( degree % n )];
            }
            else
            {
                const int rowStep = mode == Mode::Fourths ? 5 : columns;
                note              = base + row * rowStep + col;
            }
            return juce::isPositiveAndBelow( note, 128 ) ? note : -1;
        }

        bool isRoot( int Note ) const { return Note >= 0 && ( Note - root + 1200 ) % 12 == 0; }

        bool isInScale( int Note ) const
        {
            if( Note < 0 )
                return false;
            const int pc    = ( Note - root + 1200 ) % 12;
            const auto steps = scaleSteps();
            return std::find( steps.begin(), steps.end(), pc ) != steps.end();
        }
    };
} // namespace mt
