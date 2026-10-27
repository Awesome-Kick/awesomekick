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

    private:
        juce::ValueTree paramsTree();
        void            applyStoredParams();

        AudIO::DawEngine&            engine;
        InstrumentObject*            object{ nullptr };
        std::vector<InstrumentParam> params; // copy of the engine's list, readable from the UI thread
    };
} // namespace mt
