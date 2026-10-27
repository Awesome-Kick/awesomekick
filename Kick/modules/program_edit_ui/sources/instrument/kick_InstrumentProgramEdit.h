/*
 * Program edit screen for instrument (Plugin) programs:
 *  - top row: engine, pad layout (4ths/3rds/sequential), key mode, root, scale
 *  - bottom: the engine's parameters, four per page, mapped to Q-link knobs P1..P4
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#pragma once

namespace kick_ui
{
    class InstrumentProgramEdit : public ProgEditInterface
    {
    public:
        InstrumentProgramEdit();

        void refresh() override;
        void setupHeader( kick_ui::KickModeHeader& ) override {}
        void handleCommand( CPktMgr::HWMsg_t& Msg ) override;

    private:
        static constexpr int knobsPerPage = 4;

        mt::MTInstrumentProgram* instrument();
        juce::ValueTree          layoutTree();
        void                     storeLayout();
        void                     showPage( int Page );
        int                      numPages() const;

        void paint( juce::Graphics& g ) override;
        void resized() override;

        ui::KickListBox engine{ ui::KickListBox::BoxType::withTitleBar, "ENGINE", {}, 0, 1 };
        ui::KickListBox layout{ ui::KickListBox::BoxType::withTitleBar, "PAD LAYOUT", { "4ths", "3rds", "Sequential" }, 0, 1 };
        ui::KickListBox keyMode{ ui::KickListBox::BoxType::withTitleBar, "NOTES", { "In Key", "Chromatic" }, 0, 1 };
        ui::KickListBox root{ ui::KickListBox::BoxType::withTitleBar, "ROOT", mt::Note::sharpNames, 0, 3 };
        ui::KickListBox scale{ ui::KickListBox::BoxType::withTitleBar, "SCALE", mt::ScaleAndNotes::scaleNames, 0, 3 };

        std::array<std::unique_ptr<ui::KickRotary>, knobsPerPage> knobs;
        ui::KickButton  prevPage{ "prevPage", "<" }, nextPage{ "nextPage", ">" };
        ui::KickLabel   pageLabel;
        int             page{ 0 };
    };
} // namespace kick_ui
