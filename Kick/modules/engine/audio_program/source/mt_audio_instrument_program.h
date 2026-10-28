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
    class MTInstrumentProgram
        : public MTIProgram
        , private juce::Timer
    {
    public:
        /// Where programs read the project tempo (set by the audio interface; this
        /// module cannot see it). Message thread. Engines get changes within ~250 ms.
        static inline std::function<double()> tempoSource;

        explicit MTInstrumentProgram( AudIO::DawEngine& Engine, juce::ValueTree& VT, juce::UndoManager* UM = nullptr );
        ~MTInstrumentProgram() override;

        /// Engine selection. Unknown names fall back to the default engine.
        juce::String getEngineName() const;
        void         setEngine( const juce::String& Name );

        /// Parameters of the current engine (empty if none).
        const std::vector<InstrumentParam>& getParams() const;
        float                               getParam( int Index ) const;
        void                                setParam( int Index, float Value );

        /// Patch engines (e.g. Modular). Without one, the presets are empty and the patch invalid.
        bool              hasPatch() const { return patchType.isValid(); }
        /// Factory presets, then (Modular) the user's saved ones.
        juce::StringArray getPresetNames() const;
        int               getNumFactoryPresets() const { return presetNames.size(); }
        /// The stored patch; invalid while the engine plays its default (preset 0).
        juce::ValueTree getPatch() const { return patchType.isValid() ? vTree.getChildWithName( patchType ) : juce::ValueTree{}; }
        /// The stored patch, storing the default (preset 0) first if there is none, so an
        /// editor always has a tree to change. Undoable; invalid for non-patch engines.
        juce::ValueTree getOrCreatePatch();
        /// The patch's name, or preset 0's when none is stored.
        juce::String getPatchName() const;
        /// Replaces the stored patch with a fresh copy of a preset (undoable).
        void loadPreset( int Index );
        /// Saves the current patch as a user preset and names the patch after it.
        /// False for an empty name or a factory preset's.
        bool savePreset( const juce::String& Name );
        bool deleteUserPreset( const juce::String& Name );

        /// A parameter's display name; for Modular, macros are named by the patch
        /// ("Cutoff", or what the user called them).
        juce::String getParamName( int Index ) const;

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

        /// A recent output of one part of the patch, for the editor's meters.
        float getActivity( int PartId ) const
        {
            auto* e = object->getEngine();
            return e ? e->getActivity( PartId ) : 0.f;
        }

        /// The program's audio object (routing and diagnostics).
        const InstrumentObject* getAudioObject() const noexcept { return object; }

    private:
        void valueTreePropertyChanged( juce::ValueTree& Tree, const juce::Identifier& Property ) override;
        void timerCallback() override; // passes tempo changes to the engine
        double sentTempo{ 0 };
        void valueTreeChildAdded( juce::ValueTree& Parent, juce::ValueTree& Child ) override;
        void valueTreeChildRemoved( juce::ValueTree& Parent, juce::ValueTree& Child, int Index ) override;
        void valueTreeChildOrderChanged( juce::ValueTree& Parent, int, int ) override;

        juce::ValueTree paramsTree();
        void            applyStoredParams();
        void            loadEngine( const juce::String& Name );

        /// True for the patch child itself and anything inside it.
        bool isPatchTree( const juce::ValueTree& Tree ) const;
        void sendPatch();
        bool hasUserPresets() const { return patchType == modular::PatchIDs::MODULAR_PATCH; }
        void replacePatch( juce::ValueTree NewPatch );

        juce::String      loadedEngine;
        juce::Identifier  patchType;   // the engine's, or invalid
        juce::StringArray presetNames; // copied from the engine, like `params`
        int               patchSendsBlocked{ 0 };

        AudIO::DawEngine&            engine;
        InstrumentObject*            object{ nullptr };
        std::vector<InstrumentParam> params; // copy of the engine's list, readable from the UI thread
    };
} // namespace mt
