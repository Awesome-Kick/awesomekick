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
        // Logical panel size. Layout follows the hardware front panel (measured from a photo,
        // scale 2.586 board units per photo pixel, so the 1024x600 screen matches the display).
        static constexpr int boardWidth  = 1800;
        static constexpr int boardHeight = 1790;
        // Studio view (KICKEM_VIEW=studio, F2 or the Studio | Panel chip): landscape layout after the
        // MPC Live style, the basis for later tablet / phone layouts.
        static constexpr int studioWidth  = 2560;
        static constexpr int studioHeight = 1440;

        enum class View { panel, studio };

        SimulatorBoard();
        ~SimulatorBoard() override;

        void paint( juce::Graphics& g ) override;
        void resized() override;
        /// Space bar: PLAY when stopped, STOP while playing.
        bool keyPressed( const juce::KeyPress& key ) override;
        void visibilityChanged() override;
        void togglePlayStop();

        /// Switch the layout; the board takes the view's size (boardWidth x boardHeight or studioWidth x studioHeight).
        void setView( View v );
        View getView() const { return view; }
        /// Asked to switch the view (chip, F2); the owner switches and refits the window.
        std::function<void( View )> onViewRequested;

    private:
        /// Keys first go to the focused component (often a firmware button, which JUCE "clicks" on space),
        /// so this listener is attached to whatever has the focus: its key listeners are asked first.
        struct SpaceBar : juce::KeyListener, juce::FocusChangeListener
        {
            explicit SpaceBar( SimulatorBoard& b ) : board( b ) {}
            bool keyPressed( const juce::KeyPress& key, juce::Component* origin ) override;
            void globalFocusChanged( juce::Component* focused ) override;
            void attach( juce::Component* c );
            SimulatorBoard&                            board;
            juce::Component::SafePointer<juce::Component> attached;
        };
        SpaceBar spaceBar { *this };

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
        /// KICKEM_SELFTEST=popup: open a list pop-up on the Main screen, report where it lands and what a click on it hits.
        void startPopupTest();
        /// KICKEM_SELFTEST=picker: switch the track to a new Plugin program through its program picker (scaled mouse click included).
        void startPickerTest();
        /// KICKEM_SELFTEST=resources: play a kit with System Resources open, log its CPU / DSP / RAM readings.
        void startResourcesTest();
        /// KICKEM_SELFTEST=record: REC, play pads into the sequence, STOP; check the notes, then undo the take.
        void startRecordTest();
        /// KICKEM_SELFTEST=modular: open the Modular patch editor, add/move/modulate modules
        /// through its controls and the Q-links, undo, and snapshot each step.
        void startModularTest();
        /// KICKEM_SELFTEST=makedemo: build the playable demo projects (see kick_Simulator.cpp)
        /// "AwesomeKick Groove" + "Rusty Kit" (sequence, drum program, 2-bar pattern), save them and quit.
        void startMakeDemo();
        /// KICKEM_SHOWCASE=1: load "AwesomeKick Groove", play it and (with KICKEM_SHOT=<folder>) save a full-size
        /// picture of the board as awesomekick-<view>.png, then quit.
        void startShowcase();
        /// Copies the playable bundled demos (<data>/demos) into the projects folder, see kick_Simulator.cpp.
        void installBundledContent();
        void timerCallback() override;
        std::vector<std::function<void()>> selfTestSteps;
        size_t                             selfTestIndex { 0 };
        std::unique_ptr<juce::Component>   popupTestContent;

        juce::Component& screen;
        View             view { View::panel };
        SimViewSwitch    viewSwitch;

        /// Board coordinates of the firmware screen (1024x600 shown scaled) and of the LINK recess in the current view.
        juce::Rectangle<int> screenArea() const;
        juce::Rectangle<int> linkArea() const;
        void                 paintPanel( juce::Graphics& g );
        void                 paintStudio( juce::Graphics& g );

        // Left column
        /// Tooltips: the panel's own texts (tips), then a component's own tooltip, then the name of an
        /// icon button of the firmware UI (most have one, e.g. "Metronome").
        struct Tooltips : juce::TooltipWindow
        {
            /// Larger, panel-coloured tooltips (the default font is tiny once the board is scaled down).
            struct Look : juce::LookAndFeel_V4
            {
                juce::TextLayout layout( const juce::String& text, juce::Colour colour ) const;
                juce::Rectangle<int> getTooltipBounds( const juce::String& text, juce::Point<int> pos, juce::Rectangle<int> parent ) override;
                void drawTooltip( juce::Graphics& g, const juce::String& text, int w, int h ) override;
            } look;
            Tooltips( juce::Component* parent ) : juce::TooltipWindow( parent, 600 ) { setLookAndFeel( &look ); }
            ~Tooltips() override { setLookAndFeel( nullptr ); }
            juce::String getTipFor( juce::Component& c ) override;
            std::map<const juce::Component*, juce::String> tips;
        };
        Tooltips tooltips { this };
        void setupTooltips();

        SimTouchSlider slider;
        SimKnob        masterKnob { "MASTER", SimKnob::Mode::absolute };
        SimKnob        recKnob { "REC", SimKnob::Mode::absolute };
        SimButton      fullLevelBtn { "FULL LEVEL", DCC::EN_FULL_LEVEL, "HALF LVL", DCC::EN_HALF_LEVEL };
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
        /// F2: switch between the panel and the studio view.
        bool keyPressed( const juce::KeyPress& key ) override;
        void setView( SimulatorBoard::View v );
        void toggleView();
        /// Board size of the current view (the window keeps its aspect ratio).
        juce::Point<int> getBoardSize() const { return { board.getWidth(), board.getHeight() }; }
        /// Called after a view switch, e.g. so the window can adapt its aspect ratio.
        std::function<void()> onViewChanged;

    private:
        SimulatorBoard board;
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR( Simulator )
    };
}
