#pragma once

#include <map>
#include "kick_SimulatorControls.h"
#include "kick_SimulatorMainControl.h"

namespace sim
{
    ///
    /// The simulated KICK front panel at fixed logical size, with the real Kick UI (kick_ui::KickHome, 1024x600)
    /// embedded where the hardware screen is.
    ///
    class SimulatorBoard : public juce::Component
        , private juce::Timer
    {
    public:
        static constexpr int boardWidth  = 1800;
        static constexpr int boardHeight = 960;

        SimulatorBoard();
        ~SimulatorBoard() override;

        void paint( juce::Graphics& g ) override;
        void resized() override;

    private:
        /// Packets the app sent to the "hardware" (LEDs, pads, slider).
        void handleOutput( const CPktMgr::HWMsg_t& m );
        void setButtonLed( int id, bool on );
        SimButton* findButton( int code );

        /// KICKEM_SELFTEST=1: drive every control type once through the same code paths as mouse clicks.
        void startSelfTest();
        /// KICKEM_SELFTEST=instrument: exercise an instrument program and its edit screen.
        void startInstrumentTest();
        /// KICKEM_SELFTEST=audio: list audio device types and snapshot Settings > Audio.
        void startAudioSettingsTest();
        /// KICKEM_SELFTEST=tour: press every panel button (and its SHIFT function), log and snapshot each screen.
        void startTourTest();
        void timerCallback() override;
        std::vector<std::function<void()>> selfTestSteps;
        size_t                             selfTestIndex { 0 };

        juce::Component& screen;

        // Left column
        SimTouchSlider slider;
        SimKnob        masterKnob { "MASTER", SimKnob::Mode::absolute };
        SimKnob        recKnob { "REC GAIN", SimKnob::Mode::absolute };
        SimButton      fullLevelBtn { "FULL LEVEL", DCC::EN_FULL_LEVEL, "HALF LEVEL", DCC::EN_HALF_LEVEL };
        SimButton      levels16Btn { "16 LEVELS", DCC::EN_16LEVELS, "NOTES", DCC::EN_NOTES };
        SimButton      eraseBtn { "ERASE", DCC::EN_ERASE };
        SimPadGrid     pads;

        // Below the screen
        SimulatorMainControl mainControl;

        // Right column
        std::array<std::unique_ptr<SimKnob>, 4> qlinks;
        SimButton                               linkBtn { "LINK", DCC::EN_LINK };
        SimButton                               recBtn { "REC", DCC::EN_RECORD };
        SimButton                               overdubBtn { "OVERDUB", DCC::EN_OVERDUB };
        SimButton                               stopBtn { "STOP", DCC::EN_STOP };
        SimButton                               playBtn { "PLAY", DCC::EN_PLAY };
        SimButton                               playStartBtn { "PLAY START", DCC::EN_PLAYSTART };

        std::multimap<int, SimButton*> ledMap; // BtnLedSequence id -> button(s)
        sigslot::scoped_connection     outputConnection;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR( SimulatorBoard )
    };

    ///
    /// Window content: scales the board uniformly to fit any window size.
    ///
    class Simulator : public juce::Component
    {
    public:
        Simulator();
        void paint( juce::Graphics& g ) override;
        void resized() override;

    private:
        SimulatorBoard board;
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR( Simulator )
    };
}
