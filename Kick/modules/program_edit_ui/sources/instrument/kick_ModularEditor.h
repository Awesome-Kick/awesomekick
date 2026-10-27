/*
 * Patch editor for Modular instrument programs, opened from the instrument edit
 * page (EDIT PATCH):
 *  - left: the patch grid. Tap an empty cell to add a module, tap a module to
 *    select it, drag it to move it (onto another module to swap them). Audio
 *    runs down each column; lines show the flow, ~ marks modulated modules.
 *  - right: the selected module. PARAMS shows its knobs, MODS the modulations
 *    into it (depth -1..1); four per page on Q-links P1..P4. The centre knob
 *    moves the selection through the grid.
 *
 * Every change is an undoable edit of the program's patch tree (PatchEditor);
 * the editor redraws from the tree, so undo, redo and preset loads show up too.
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#pragma once

namespace kick_ui
{
    class ModularEditor
        : public juce::Component
        , private juce::ValueTree::Listener
        , private juce::AsyncUpdater
    {
    public:
        static constexpr int knobsPerPage = 4;

        std::function<void()> onClose;

        ModularEditor();
        ~ModularEditor() override;

        /// nullptr detaches (e.g. the page shows another program).
        void setProgram( mt::MTInstrumentProgram* Program );

        /// Q-links: Knob 0..3. The centre knob (-1) moves the selection.
        void turnKnob( int Knob, int Steps );

        // What a touch does, also used by the simulator self-test.
        void selectCell( int Col, int Row );
        void addModule( const juce::String& Type ); // in the selected cell
        void deleteModule();
        void moveModule( int Col, int Row ); // the selected module, then follows it
        void addModulation( const juce::String& Param, const juce::String& Source, float Depth = 0.5f );
        void showMods( bool Mods );

        juce::Point<int> getSelectedCell() const { return { selCol, selRow }; }

    private:
        class Grid;
        friend class Grid;

        enum class Mode
        {
            Params,
            Mods
        };

        void refresh();
        void refreshInspector();
        void showPage( int Page );
        int  numItems() const; // params or mods of the selected module
        void applyKnob( int Knob, double Value );

        juce::ValueTree             view() const; // the stored patch, or the default it plays
        mt::modular::PatchEditor    reader() const { return { view(), nullptr }; }
        /// Begins an undo transaction, except while the same gesture (e.g. one knob
        /// being turned) continues, so turning a knob undoes as one step.
        mt::modular::PatchEditor    edit( const juce::String& Gesture = {} );
        juce::ValueTree             selectedModule() const;
        std::vector<juce::ValueTree> selectedMods() const;

        void pick( const juce::String& Title, const juce::StringArray& Options, std::function<void( int )> OnPick );
        void pickModuleType();
        void pickModulation();
        void pickVoices();

        void handleAsyncUpdate() override { refresh(); }
        void valueTreePropertyChanged( juce::ValueTree&, const juce::Identifier& ) override { triggerAsyncUpdate(); }
        void valueTreeChildAdded( juce::ValueTree&, juce::ValueTree& ) override { triggerAsyncUpdate(); }
        void valueTreeChildRemoved( juce::ValueTree&, juce::ValueTree&, int ) override { triggerAsyncUpdate(); }

        void paint( juce::Graphics& g ) override;
        void resized() override;

        mt::MTInstrumentProgram* program{ nullptr };
        juce::ValueTree          programState;   // listened to
        juce::ValueTree          defaultPatch;   // shown until a patch is stored

        int  selCol{ 0 }, selRow{ 0 };
        Mode mode{ Mode::Params };
        int  page{ 0 };
        int  selectedMod{ -1 }; // index into selectedMods(), for REMOVE MOD

        juce::String lastGesture;
        juce::uint32 lastEditMs{ 0 };

        std::unique_ptr<Grid> grid;

        ui::KickButton back{ "modularBack", "< BACK" };
        ui::KickLabel  title;
        ui::KickButton voices{ "modularVoices", "VOICES" };

        ui::KickLabel  moduleTitle, hint;
        ui::KickButton modeButton{ "modularMode", "PARAMS" };
        ui::KickButton addModuleButton{ "modularAdd", "+ MODULE" };
        ui::KickButton deleteButton{ "modularDelete", "DELETE" };
        ui::KickButton addModButton{ "modularAddMod", "+ MOD" };
        ui::KickButton removeModButton{ "modularRemoveMod", "REMOVE MOD" };

        std::array<std::unique_ptr<ui::KickRotary>, knobsPerPage> knobs;
        ui::KickButton prevPage{ "modularPrev", "<" }, nextPage{ "modularNext", ">" };
        ui::KickLabel  pageLabel;

        ui::KickList picker{ "picker" };
    };
} // namespace kick_ui
