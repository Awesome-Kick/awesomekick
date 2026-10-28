/*
 * Patch editor for Modular instrument programs, opened from the instrument edit
 * page (EDIT PATCH):
 *  - left: the patch grid. Tap an empty cell to add a module, tap a module to
 *    select it, drag it to move it (onto another module to swap them). Audio
 *    runs down each column; lines show the flow, curves show cables, ~ marks
 *    modulated modules. Below the grid, the effect slots the voices go through.
 *  - right: the selected module or effect. PARAMS shows its knobs, MODS the
 *    modulations into it, IN the cables into it (depth/amount -1..1); four per
 *    page on Q-links P1..P4. The centre knob moves the selection.
 *  - top: SAVE (or delete a user preset), MACROS (rename them), ARP (pattern,
 *    rate, octaves, gate), VOICES (poly count, mono, legato, glide).
 *  - while notes play, each module shows its activity: modulators their value,
 *    audio modules and effects their level.
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
        , private juce::Timer
    {
    public:
        static constexpr int knobsPerPage = 4;
        /// The selection row of the effect slots, below the module rows.
        static constexpr int effectRow = mt::modular::gridRows;

        std::function<void()> onClose;

        ModularEditor();
        ~ModularEditor() override;

        /// nullptr detaches (e.g. the page shows another program).
        void setProgram( mt::MTInstrumentProgram* Program );

        /// Q-links: Knob 0..3. The centre knob (-1) moves the selection.
        void turnKnob( int Knob, int Steps );

        // What a touch does, also used by the simulator self-test.
        void selectCell( int Col, int Row ); // Row == effectRow: effect slot Col
        void addModule( const juce::String& Type ); // a module or an effect, in the selected cell
        void deleteModule();
        void moveModule( int Col, int Row ); // the selected module, then follows it
        void moveEffect( int Slot );         // the selected effect, then follows it
        void addModulation( const juce::String& Param, const juce::String& Source, float Depth = 0.5f );
        void addCable( const juce::String& Source, float Amount = 0.5f );

        enum class Mode
        {
            Params,
            Mods,
            Inputs
        };
        void showMode( Mode M );

        juce::Point<int> getSelectedCell() const { return { selCol, selRow }; }

    private:
        class Grid;
        friend class Grid;

        void refresh();
        void refreshInspector();
        void showPage( int Page );
        int  numItems() const; // params, mods or cables of the selection
        void applyKnob( int Knob, double Value );

        juce::ValueTree             view() const; // the stored patch, or the default it plays
        mt::modular::PatchEditor    reader() const { return { view(), nullptr }; }
        /// Begins an undo transaction, except while the same gesture (e.g. one knob
        /// being turned) continues, so turning a knob undoes as one step.
        mt::modular::PatchEditor    edit( const juce::String& Gesture = {} );
        juce::ValueTree             selectedModule() const; // module or effect
        bool                        effectSelected() const { return selRow == effectRow; }
        std::vector<juce::ValueTree> selectedLinks( const juce::Identifier& Type ) const; // MOD or CABLE children
        std::vector<juce::ValueTree> selectedItems() const; // the links the current mode shows

        void pick( const juce::String& Title, const juce::StringArray& Options, std::function<void( int )> OnPick );
        void pickType();
        void pickLink(); // + MOD or + CABLE
        void pickVoices();
        void pickGlide();
        void pickArp();
        juce::String arpText() const;
        void pickSaveOrDelete();
        void deleteUserPreset();
        juce::String voicesText() const;
        void pickMacro();
        void askName( const juce::String& Title, const juce::String& Initial, std::function<void( const juce::String& )> OnName );
        void savePreset();

        void handleAsyncUpdate() override { refresh(); }
        void timerCallback() override; // activity meters
        void visibilityChanged() override;
        float activityOf( int Id ) const;
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
        int  selectedItem{ -1 }; // index into selectedItems(), for REMOVE

        juce::String lastGesture;
        juce::uint32 lastEditMs{ 0 };

        std::map<int, float> activity; // part id -> latest value, as drawn

        std::unique_ptr<Grid> grid;

        ui::KickButton back{ "modularBack", "< BACK" };
        ui::KickLabel  title;
        ui::KickButton macros{ "modularMacros", "MACROS" };
        ui::KickButton save{ "modularSave", "SAVE" };
        ui::KickButton voices{ "modularVoices", "VOICES" };
        ui::KickButton arp{ "modularArp", "ARP" };

        ui::KickLabel  moduleTitle, hint;
        ui::KickButton paramsTab{ "modularParams", "PARAMS" }, modsTab{ "modularMods", "MODS" }, inputsTab{ "modularInputs", "IN" };
        ui::KickButton addButton{ "modularAdd", "+ MODULE" };
        ui::KickButton deleteButton{ "modularDelete", "DELETE" };
        ui::KickButton linkButton{ "modularLink", "+ MOD" };
        ui::KickButton removeButton{ "modularRemove", "REMOVE" };

        std::array<std::unique_ptr<ui::KickRotary>, knobsPerPage> knobs;
        ui::KickButton prevPage{ "modularPrev", "<" }, nextPage{ "modularNext", ">" };
        ui::KickLabel  pageLabel;

        ui::KickList picker{ "picker" };
    };
} // namespace kick_ui
