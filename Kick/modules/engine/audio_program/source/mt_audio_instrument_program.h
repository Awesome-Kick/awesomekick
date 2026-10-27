/*
 * Plugin ("instrument") program: notes drive an IInstrumentEngine instead of
 * samples. The engine name and its parameter values are stored in the program's
 * ValueTree, so they are saved with the project and restored on load.
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#pragma once

namespace mt
{
    class MTInstrumentProgram : public MTIProgram
    {
    public:
        explicit MTInstrumentProgram( AudIO::DawEngine& Engine, juce::ValueTree& VT, juce::UndoManager* UM = nullptr );
        ~MTInstrumentProgram() override;

        /// Engine selection. Unknown names fall back to the default engine.
        juce::String getEngineName() const;
        void         setEngine( const juce::String& Name );

        /// Parameters of the current engine (empty if none).
        const std::vector<InstrumentParam>& getParams() const;
        float                               getParam( int Index ) const;
        void                                setParam( int Index, float Value );

        /// Adds all-notes-off / all-sound-off handling to MTIProgram's note handling:
        /// tracks send CC 123 on transport stop and program change, and the base class
        /// only understands note on/off, which would leave instrument notes hanging.
        void noteProcess( juce::Array<juce::MidiMessage>& Msgs ) override;

        // IMidiMsgProcessor (MTIProgram's defaults already gate: note-off releases).
        void noteOn( int Note, int Velocity ) override { object->noteOn( Note, Velocity ); }
        void noteOff( int Note ) override { object->noteOff( Note ); }
        void noteOff( std::vector<int>& Notes ) override
        {
            for( auto n : Notes )
                object->noteOff( n );
        }
        void noteOffAll() override { object->allNotesOff(); }
        void noteReset( int Note ) override { object->noteOff( Note ); }

        void               enableOutputMetering( bool ) override {}
        const MTMeterData& readOutMeter() const override { return object->readOutMeter(); }

        /// The program's audio object (routing and diagnostics).
        const InstrumentObject* getAudioObject() const noexcept { return object; }

    private:
        void valueTreePropertyChanged( juce::ValueTree& Tree, const juce::Identifier& Property ) override;

        juce::ValueTree paramsTree();
        void            applyStoredParams();
        void            loadEngine( const juce::String& Name );

        juce::String loadedEngine;

        AudIO::DawEngine&            engine;
        InstrumentObject*            object{ nullptr };
        std::vector<InstrumentParam> params; // copy of the engine's list, readable from the UI thread
    };
} // namespace mt
