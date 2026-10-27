#include <kick_ui/kick_ui.h>
#include "kick_Simulator.h"
#include "kick_SimulatorInterfaceImpl.h"

namespace sim
{
    namespace
    {
        const juce::Rectangle<int> screenRect { 620, 20, 1024, 600 };

        /// Diagnostics (set KICKEM_TRACE_MOUSE=1): log every mouse-down the app receives.
        struct MouseTrace : juce::MouseListener
        {
            void mouseDown( const juce::MouseEvent& e ) override
            {
                SimulatorInterfaceImpl::log( "mouseDown on " + juce::String( typeid( *e.eventComponent ).name() ) + " at " + e.getScreenPosition().toString()
                                             + " modal=" + juce::String( juce::Component::getNumCurrentlyModalComponents() ) );
            }
        } mouseTrace;
    }

    //==============================================================================
    // SimulatorBoard
    //==============================================================================
    SimulatorBoard::SimulatorBoard()
        : screen( kick_ui::KickHome::Instance() )
    {
        addAndMakeVisible( screen );

        addAndMakeVisible( slider );
        addAndMakeVisible( masterKnob );
        addAndMakeVisible( recKnob );
        addAndMakeVisible( fullLevelBtn );
        addAndMakeVisible( levels16Btn );
        addAndMakeVisible( eraseBtn );
        addAndMakeVisible( pads );
        addAndMakeVisible( mainControl );

        for( int i = 0; i < 4; ++i )
        {
            qlinks[i] = std::make_unique<SimKnob>( "P" + juce::String( i + 1 ), SimKnob::Mode::endless );
            qlinks[i]->onStep = [i]( int steps ) {
                hw::send( hw::knobMessage( DCC::EN_KNOB_1 + i, steps ) );
            };
            addAndMakeVisible( *qlinks[i] );
        }
        for( auto* b : { &linkBtn, &recBtn, &overdubBtn, &stopBtn, &playBtn, &playStartBtn } )
            addAndMakeVisible( b );

        recBtn.setLedColour( juce::Colour( 0xffff2020 ) );
        overdubBtn.setLedColour( juce::Colour( 0xffff2020 ) );
        playBtn.setLedColour( juce::Colour( 0xff20e040 ) );
        playStartBtn.setLedColour( juce::Colour( 0xff20e040 ) );

        // LED routing table: both functions of a dual-function button share its LED.
        std::vector<SimButton*> buttons { &fullLevelBtn, &levels16Btn, &eraseBtn, &linkBtn, &recBtn, &overdubBtn, &stopBtn, &playBtn, &playStartBtn };
        mainControl.collectButtons( buttons );
        for( auto* b : buttons )
        {
            if( b->getCode() != DCC::EN_INVALID_ID ) ledMap.emplace( b->getCode(), b );
            if( b->getShiftCode() != DCC::EN_INVALID_ID ) ledMap.emplace( b->getShiftCode(), b );
        }

        ShiftLatch::onChange = [this]( bool latched ) {
            mainControl.getShiftButton().setLed( latched );
            repaint(); // shift legends brighten while latched
        };

        outputConnection = SimulatorInterfaceImpl::outputReceived.connect( [this]( const CPktMgr::HWMsg_t& m ) { handleOutput( m ); } );

        setSize( boardWidth, boardHeight ); // last: resized() needs every child

        if( juce::SystemStats::getEnvironmentVariable( "KICKEM_TRACE_MOUSE", {} ).isNotEmpty() )
            juce::Desktop::getInstance().addGlobalMouseListener( &mouseTrace );

        if( juce::SystemStats::getEnvironmentVariable( "KICKEM_SELFTEST", {} ).isNotEmpty() )
            startSelfTest();
    }

    SimButton* SimulatorBoard::findButton( int code )
    {
        auto range = ledMap.equal_range( code );
        for( auto it = range.first; it != range.second; ++it )
            if( it->second->getCode() == code )
                return it->second;
        return nullptr;
    }

    void SimulatorBoard::startSelfTest()
    {
        auto& s     = selfTestSteps;
        auto  note  = [&s]( juce::String text ) { s.push_back( [text] { SimulatorInterfaceImpl::log( "SELFTEST " + text ); } ); };
        auto  wait  = [&s]( int n ) { for( int i = 0; i < n; ++i ) s.push_back( [] {} ); };
        auto  click = [&s, this]( int code ) {
            s.push_back( [this, code] { if( auto* b = findButton( code ) ) b->press( true ); } );
            s.push_back( [this, code] { if( auto* b = findButton( code ) ) b->press( false ); } );
        };
        auto pad = [&s, this]( int i ) {
            s.push_back( [this, i] { pads.getPad( i ).hit( true, 100 ); } );
            s.push_back( [this, i] { pads.getPad( i ).hit( false ); } );
        };

        if( juce::SystemStats::getEnvironmentVariable( "KICKEM_SELFTEST", {} ) == "instrument" )
        {
            startInstrumentTest();
            return;
        }
        if( juce::SystemStats::getEnvironmentVariable( "KICKEM_SELFTEST", {} ) == "audio" )
        {
            startAudioSettingsTest();
            return;
        }
        if( juce::SystemStats::getEnvironmentVariable( "KICKEM_SELFTEST", {} ) == "tour" )
        {
            startTourTest();
            return;
        }
        if( juce::SystemStats::getEnvironmentVariable( "KICKEM_SELFTEST", {} ) == "popup" )
        {
            startPopupTest();
            return;
        }
        if( juce::SystemStats::getEnvironmentVariable( "KICKEM_SELFTEST", {} ) == "picker" )
        {
            startPickerTest();
            return;
        }

        wait( 4 );
        note( "jog ENTER (load selected project)" );
        s.push_back( [this] { mainControl.getJog().pushCentre( true ); } );
        s.push_back( [this] { mainControl.getJog().pushCentre( false ); } );
        wait( 12 );
        note( "pads 1 and 6" );
        pad( 0 );
        pad( 5 );
        note( "bank B" );
        click( DCC::EN_BANK_B );
        wait( 2 );
        note( "SHIFT + bank C (-> bank G)" );
        s.push_back( [this] { mainControl.getShiftButton().press( true ); } );
        s.push_back( [this] { mainControl.getShiftButton().press( false ); } );
        click( DCC::EN_BANK_C );
        wait( 2 );
        note( "bank A" );
        click( DCC::EN_BANK_A );
        note( "PLAY" );
        click( DCC::EN_PLAY );
        wait( 3 );
        note( "STOP" );
        click( DCC::EN_STOP );
        note( "jog +3 / -2" );
        s.push_back( [this] { mainControl.getJog().turn( 3 ); } );
        s.push_back( [this] { mainControl.getJog().turn( -2 ); } );
        note( "Q-link P1..P4 +2 / -1" );
        for( int q = 0; q < 4; ++q )
        {
            s.push_back( [this, q] { qlinks[(size_t)q]->turn( 2 ); } );
            s.push_back( [this, q] { qlinks[(size_t)q]->turn( -1 ); } );
        }
        note( "slider 30 / 70" );
        s.push_back( [this] { slider.setTouch( 30 ); } );
        s.push_back( [this] { slider.setTouch( 70 ); } );
        note( "LINK, ERASE, MAIN" );
        click( DCC::EN_LINK );
        click( DCC::EN_ERASE );
        click( DCC::EN_MAINMODE );
        note( "done" );

        startTimer( 400 );
    }

    void SimulatorBoard::startInstrumentTest()
    {
        // KICKEM_SELFTEST=instrument: create an instrument program, open its edit screen,
        // drive Q-links and pads, then quit. KICKEM_SHOT=<folder> saves screen snapshots.
        auto& s    = selfTestSteps;
        auto  log  = []( juce::String text ) { SimulatorInterfaceImpl::log( "SELFTEST " + text ); };
        auto  wait = [&s]( int n ) { for( int i = 0; i < n; ++i ) s.push_back( [] {} ); };
        auto  click = [&s, this]( int code ) {
            s.push_back( [this, code] { if( auto* b = findButton( code ) ) b->press( true ); } );
            s.push_back( [this, code] { if( auto* b = findButton( code ) ) b->press( false ); } );
        };
        auto shot = [&s, this, log]( juce::String name ) {
            s.push_back( [this, log, name] {
                const auto dir = juce::SystemStats::getEnvironmentVariable( "KICKEM_SHOT", {} );
                if( dir.isEmpty() )
                    return;
                auto file = juce::File( dir ).getChildFile( name + ".png" );
                file.deleteFile();
                if( auto out = file.createOutputStream() )
                {
                    juce::PNGImageFormat().writeImageToStream( screen.createComponentSnapshot( screen.getLocalBounds() ), *out );
                    log( "snapshot " + file.getFullPathName() );
                }
            } );
        };
        auto instrument = []() -> mt::MTInstrumentProgram* {
            auto& ae = mt::getAudioInterface();
            return dynamic_cast<mt::MTInstrumentProgram*>( ae.getProgramForTrack( ae.getSelectedSequence(), ae.getSelectedTrack() ) );
        };

        wait( 4 );
        // Work on a throwaway copy of a template: the app saves the open project on exit,
        // so loading the first project in the list would modify the user's own projects.
        s.push_back( [log] {
            auto exeDir  = juce::File::getSpecialLocation( juce::File::currentExecutableFile ).getParentDirectory();
            // The engine opens <folder>/<folder name>.kickPrj.
            auto source  = exeDir.getChildFile( "templates/Template 01/Template 01.kickPrj" );
            auto scratch = juce::File::getSpecialLocation( juce::File::tempDirectory ).getChildFile( "kickem_selftest/SelfTest" );
            scratch.getParentDirectory().deleteRecursively();
            scratch.createDirectory();
            const bool ok = source.copyFileTo( scratch.getChildFile( "SelfTest.kickPrj" ) ) && mt::getAudioInterface().loadProject( scratch );
            log( "loaded scratch project " + scratch.getFullPathName() + ( ok ? "" : " (FAILED)" ) );
        } );
        wait( 8 );

        s.push_back( [log] {
            auto& ae = mt::getAudioInterface();
            if( !ae.getNumSequences() )
                ae.createSequence();
            const auto seq  = ae.getSelectedSequence();
            const auto trk  = ae.createTrack( seq, "Synth" );
            const auto prog = ae.createProgram( mt::MTProgramType::Plugin, "Synth 1" );
            ae.setProgramToTrack( prog, seq, trk );
            ae.setSelectedSequenceAndTrack( seq, trk );
            ae.setSelectedProgram( prog );
            log( "created instrument program " + juce::String( prog ) + " on track " + juce::String( trk ) );
        } );
        wait( 2 );
        s.push_back( [log] { log( "PROGRAM EDIT" ); } );
        click( DCC::EN_PROG_EDIT );
        wait( 4 );
        s.push_back( [log] {
            log( "screen = " + juce::String( (int)kick_ui::KickHome::Instance().getCurrentScreen() ) + " (MenuProgramEdit = " + juce::String( (int)kick_ui::Screen::MenuProgramEdit ) + ")" );
        } );
        shot( "instrument_edit_page1" );

        s.push_back( [log, instrument] {
            if( auto* i = instrument() )
                log( "before: " + i->getParams()[0].name + "=" + juce::String( i->getParam( 0 ), 3 ) + "  " + i->getParams()[3].name + "=" + juce::String( i->getParam( 3 ), 1 ) );
        } );
        s.push_back( [log] { log( "Q-link P1 +20, P4 -30" ); } );
        s.push_back( [this] { qlinks[0]->turn( 20 ); } );
        s.push_back( [this] { qlinks[3]->turn( -30 ); } );
        wait( 2 );
        s.push_back( [log, instrument] {
            if( auto* i = instrument() )
                log( "after:  " + i->getParams()[0].name + "=" + juce::String( i->getParam( 0 ), 3 ) + "  " + i->getParams()[3].name + "=" + juce::String( i->getParam( 3 ), 1 ) );
        } );
        shot( "instrument_edit_after_qlinks" );

        s.push_back( [log] { log( "bank D, pads 1-4 (C3 D3 E3 F3 in C major 4ths)" ); } );
        click( DCC::EN_BANK_D );
        for( int p = 0; p < 4; ++p )
        {
            s.push_back( [this, p, log] {
                log( "pad " + juce::String( p + 1 ) + " -> note " + juce::String( kick_ui::PadNotes::noteForPad( p ) ) );
                pads.getPad( p ).hit( true, 100 );
            } );
            wait( 1 );
            s.push_back( [this, p] { pads.getPad( p ).hit( false ); } );
        }
        s.push_back( [log] { log( "done" ); } );
        wait( 2 );
        s.push_back( [] { juce::JUCEApplication::getInstance()->systemRequestedQuit(); } );

        startTimer( 400 );
    }

    void SimulatorBoard::startAudioSettingsTest()
    {
        // KICKEM_SELFTEST=audio: list device types, open Settings > Audio, snapshot, quit.
        auto& s   = selfTestSteps;
        auto  log = []( juce::String text ) { SimulatorInterfaceImpl::log( "SELFTEST " + text ); };
        auto  wait = [&s]( int n ) { for( int i = 0; i < n; ++i ) s.push_back( [] {} ); };

        wait( 6 ); // the engine opens its device asynchronously
        s.push_back( [log] {
            auto& dm = mt::getAudioInterface().getAudioDeviceManager();
            for( auto* type : dm.getAvailableDeviceTypes() )
            {
                type->scanForDevices();
                log( "device type " + type->getTypeName() + ": " + type->getDeviceNames().joinIntoString( " | " ) );
            }
            auto* dev = dm.getCurrentAudioDevice();
            log( "current: " + dm.getCurrentAudioDeviceType() + " / " + ( dev ? dev->getName() + " @ " + juce::String( dev->getCurrentSampleRate() ) + " Hz, " + juce::String( dev->getCurrentBufferSizeSamples() ) : juce::String( "none" ) ) );
        } );
        s.push_back( [] { kick_ui::signals::sigScreenClose( kick_ui::Screen::HomePreferencesView ); } );
        wait( 3 );
        s.push_back( [this, log] {
            // Click the "Audio" tab the way a user would.
            std::function<juce::Button*( juce::Component& )> find = [&find]( juce::Component& c ) -> juce::Button* {
                if( auto* b = dynamic_cast<juce::Button*>( &c ); b && b->getName() == "Audio" )
                    return b;
                for( auto* child : c.getChildren() )
                    if( auto* hit = find( *child ) )
                        return hit;
                return nullptr;
            };
            if( auto* tab = find( screen ) )
            {
                tab->triggerClick();
                log( "clicked Audio tab" );
            }
            else
                log( "Audio tab not found" );
        } );
        wait( 4 );
        s.push_back( [this, log] {
            const auto dir = juce::SystemStats::getEnvironmentVariable( "KICKEM_SHOT", {} );
            if( dir.isEmpty() )
                return;
            auto file = juce::File( dir ).getChildFile( "settings_audio.png" );
            file.deleteFile();
            if( auto out = file.createOutputStream() )
            {
                juce::PNGImageFormat().writeImageToStream( screen.createComponentSnapshot( screen.getLocalBounds() ), *out );
                log( "snapshot " + file.getFullPathName() );
            }
        } );
        s.push_back( [log] { log( "done" ); } );
        wait( 2 );
        s.push_back( [] { juce::JUCEApplication::getInstance()->systemRequestedQuit(); } );

        startTimer( 400 );
    }

    void SimulatorBoard::startTourTest()
    {
        // KICKEM_SELFTEST=tour: load a scratch copy of a kit (KICKEM_TOUR_PROJECT=<project name>,
        // default "Vintage Soul Kit"), then press every panel button and its SHIFT function from
        // the main screen. Each stop logs the screen the app reports and, with KICKEM_SHOT, saves a snapshot.
        auto& s    = selfTestSteps;
        auto  log  = []( juce::String text ) { SimulatorInterfaceImpl::log( "SELFTEST " + text ); };
        auto  wait = [&s]( int n ) { for( int i = 0; i < n; ++i ) s.push_back( [] {} ); };
        auto  screenName = []() -> juce::String {
            static const char* names[] = { "HomeOptions", "HomeListView", "HomeGridView", "HomeSequencerView", "HomeXYView", "HomePreferencesView",
                                           "HomePreferences", "MenuSampleEdit", "MenuProgramEdit", "MenuSampler", "MenuLooper", "MenuPadMute",
                                           "MenuTrackMute", "MenuPadMixer", "MenuChannelMixer", "MenuKnobEdit", "MenuListEdit", "MenuPadPerform",
                                           "MenuMidiControl", "MenuFileBrowser", "MenuNextSequence", "MenuSong", "MenuColorEdit", "MenuAssignSample",
                                           "RecentProject", "EditProgram", "SWUpdateView", "SWUpdateWindow", "ProcessEdit", "CVClib",
                                           "SystemResources", "Inserts", "Temp" };
            const int i = (int)kick_ui::KickHome::Instance().getCurrentScreen() - (int)kick_ui::Screen::HomeOptions;
            return i >= 0 && i < (int)std::size( names ) ? juce::String( names[i] ) : juce::String( i + (int)kick_ui::Screen::HomeOptions );
        };
        auto shot = [&s, this, log]( juce::String name ) {
            s.push_back( [this, log, name] {
                const auto dir = juce::SystemStats::getEnvironmentVariable( "KICKEM_SHOT", {} );
                if( dir.isEmpty() )
                    return;
                auto file = juce::File( dir ).getChildFile( name + ".png" );
                file.deleteFile();
                if( auto out = file.createOutputStream() )
                    juce::PNGImageFormat().writeImageToStream( screen.createComponentSnapshot( screen.getLocalBounds() ), *out );
            } );
        };
        auto press = [&s, this]( int code, bool shifted ) {
            s.push_back( [this, code, shifted] {
                if( auto* b = findButton( code ) )
                    b->press( true, shifted );
            } );
            s.push_back( [this, code] {
                if( auto* b = findButton( code ) )
                    b->press( false );
            } );
        };
        auto visit = [&]( juce::String label, int code, bool shifted ) {
            s.push_back( [log, label] { log( "press " + label ); } );
            press( code, shifted );
            wait( 4 );
            s.push_back( [log, label, screenName] { log( "  " + label + " -> screen " + screenName() ); } );
            shot( "tour_" + label.replace( " ", "_" ).replace( "/", "_" ) );
            // Poke the screen's controls: every Q-link, the jog and a pad. Preferences are global
            // (not part of the scratch project), so leave that screen alone.
            if( label != "PREFS" )
            {
                for( int q = 0; q < 4; ++q )
                {
                    s.push_back( [this, q] { qlinks[(size_t)q]->turn( 1 ); } );
                    s.push_back( [this, q] { qlinks[(size_t)q]->turn( -1 ); } );
                }
                s.push_back( [this] { mainControl.getJog().turn( 1 ); } );
                s.push_back( [this] { mainControl.getJog().turn( -1 ); } );
                s.push_back( [this] { pads.getPad( 0 ).hit( true, 100 ); } );
                s.push_back( [this] { pads.getPad( 0 ).hit( false ); } );
                s.push_back( [log, label, screenName] { log( "  " + label + " controls ok, screen " + screenName() ); } );
            }
            // Back to the main screen for the next stop.
            press( DCC::EN_MAINMODE, false );
            wait( 3 );
        };

        wait( 4 );
        s.push_back( [log] {
            auto exeDir = juce::File::getSpecialLocation( juce::File::currentExecutableFile ).getParentDirectory();
            auto name   = juce::SystemStats::getEnvironmentVariable( "KICKEM_TOUR_PROJECT", "Vintage Soul Kit" );
            auto source = exeDir.getChildFile( "projects" ).getChildFile( name );
            if( !source.isDirectory() )
                source = exeDir.getChildFile( "templates/Template 01" );
            // The engine opens <folder>/<folder name>.kickPrj, so keep the project's folder name.
            auto root = juce::File::getSpecialLocation( juce::File::tempDirectory ).getChildFile( "kickem_tour" );
            root.deleteRecursively();
            auto       scratch = root.getChildFile( source.getFileName() );
            const bool ok      = source.copyDirectoryTo( scratch ) && mt::getAudioInterface().loadProject( scratch );
            auto&      ae      = mt::getAudioInterface();
            log( "loaded scratch copy of " + source.getFileName() + ( ok ? "" : " (FAILED)" ) + ": " + juce::String( ae.getNumSequences() ) + " sequence(s), "
                 + juce::String( ae.getNumPrograms() ) + " program(s)" );
        } );
        wait( 8 );
        press( DCC::EN_MAINMODE, false );
        wait( 3 );
        s.push_back( [log, screenName] {
            auto& ae = mt::getAudioInterface();
            log( "main screen = " + screenName() + ", sequences " + juce::String( ae.getNumSequences() ) );
        } );
        shot( "tour_MAIN" );

        // Transport from the main screen.
        s.push_back( [log] { log( "press PLAY" ); } );
        press( DCC::EN_PLAY, false );
        wait( 5 );
        s.push_back( [log] { log( juce::String( "  playing = " ) + ( mt::getAudioInterface().isSequencePlaying() ? "yes" : "no" ) ); } );
        shot( "tour_PLAYING" );
        s.push_back( [log] { log( "press STOP" ); } );
        press( DCC::EN_STOP, false );
        wait( 2 );
        s.push_back( [log] { log( juce::String( "  playing = " ) + ( mt::getAudioInterface().isSequencePlaying() ? "yes" : "no" ) ); } );

        struct Stop
        {
            const char* label;
            int         code;
            bool        shifted;
        };
        const Stop stops[] = {
            { "GRID", DCC::EN_MAINMODE, true },          { "BROWSE", DCC::EN_BROWSE, false },
            { "TRACK MIX", DCC::EN_TRACK_MIX, false },   { "PAD MIX", DCC::EN_TRACK_MIX, true },
            { "TRACK MUTE", DCC::EN_TRACK_MUTE, false }, { "PAD MUTE", DCC::EN_TRACK_MUTE, true },
            { "NEXT SEQ", DCC::EN_NEXT_SEQ, false },     { "FX", DCC::EN_NEXT_SEQ, true },
            { "STEP SEQ", DCC::EN_STEP_SEQ, false },     { "AUTOMATION", DCC::EN_STEP_SEQ, true },
            { "TAP TEMPO", DCC::EN_TAP_TEMPO, false },   { "MASTER", DCC::EN_TAP_TEMPO, true },
            { "NOTE REPEAT", DCC::EN_NOTE_REPEAT, false }, { "LATCH", DCC::EN_NOTE_REPEAT, true },
            { "TC", DCC::EN_TC, false },                 { "ON-OFF", DCC::EN_TC, true },
            { "SAMPLER", DCC::EN_SAMPLER, false },       { "LOOPER", DCC::EN_SAMPLER, true },
            { "SAMPLE EDIT", DCC::EN_SAMPLE_EDIT, false }, { "LINK EDIT", DCC::EN_SAMPLE_EDIT, true },
            { "PROGRAM EDIT", DCC::EN_PROG_EDIT, false }, { "MIDI CONTROL", DCC::EN_PROG_EDIT, true },
            { "MENU", DCC::EN_MENU, false },             { "PREFS", DCC::EN_MENU, true },
            { "FULL LEVEL", DCC::EN_FULL_LEVEL, false }, { "HALF LEVEL", DCC::EN_FULL_LEVEL, true },
            { "16 LEVELS", DCC::EN_16LEVELS, false },    { "NOTES", DCC::EN_16LEVELS, true },
            { "COPY", DCC::EN_COPY, false },             { "LINK", DCC::EN_LINK, false },
            { "REC", DCC::EN_RECORD, false },            { "OVERDUB", DCC::EN_OVERDUB, false },
            { "PLAY START", DCC::EN_PLAYSTART, false },  { "STOP", DCC::EN_STOP, false },
        };
        // KICKEM_TOUR_FROM=<label> starts the button tour at that stop.
        auto from    = juce::SystemStats::getEnvironmentVariable( "KICKEM_TOUR_FROM", {} );
        bool started = from.isEmpty();
        for( const auto& stop : stops )
        {
            started = started || from == stop.label;
            if( started )
                visit( stop.label, stop.code, stop.shifted );
        }

        s.push_back( [log] { log( "done" ); } );
        wait( 2 );
        s.push_back( [] { juce::JUCEApplication::getInstance()->systemRequestedQuit(); } );

        startTimer( 400 );
    }

    void SimulatorBoard::startPopupTest()
    {
        auto& s    = selfTestSteps;
        auto  log  = []( juce::String text ) { SimulatorInterfaceImpl::log( "SELFTEST " + text ); };
        auto  wait = [&s]( int n ) { for( int i = 0; i < n; ++i ) s.push_back( [] {} ); };
        auto  click = [&s, this]( int code ) {
            s.push_back( [this, code] { if( auto* b = findButton( code ) ) b->press( true ); } );
            s.push_back( [this, code] { if( auto* b = findButton( code ) ) b->press( false ); } );
        };
        auto windowShot = [&s, this, log]( juce::String name ) {
            s.push_back( [this, log, name] {
                const auto dir = juce::SystemStats::getEnvironmentVariable( "KICKEM_SHOT", {} );
                auto*      top = getTopLevelComponent();
                if( dir.isEmpty() || !top )
                    return;
                auto file = juce::File( dir ).getChildFile( name + ".png" );
                file.deleteFile();
                if( auto out = file.createOutputStream() )
                    juce::PNGImageFormat().writeImageToStream( top->createComponentSnapshot( top->getLocalBounds() ), *out );
                log( "window snapshot " + file.getFullPathName() );
            } );
        };

        wait( 4 );
        s.push_back( [log] {
            auto exeDir = juce::File::getSpecialLocation( juce::File::currentExecutableFile ).getParentDirectory();
            auto source = exeDir.getChildFile( "projects/Vintage Soul Kit" );
            auto root   = juce::File::getSpecialLocation( juce::File::tempDirectory ).getChildFile( "kickem_popup" );
            root.deleteRecursively();
            auto scratch = root.getChildFile( source.getFileName() );
            const bool ok = source.copyDirectoryTo( scratch ) && mt::getAudioInterface().loadProject( scratch );
            log( juce::String( "loaded scratch kit" ) + ( ok ? "" : " (FAILED)" ) );
        } );
        wait( 8 );
        click( DCC::EN_MAINMODE ); // the Main screen's track row has list boxes (I/P MIDI PORT, channel)
        wait( 4 );
        s.push_back( [this, log] {
            std::function<ui::KickListBox*( juce::Component& )> find = [&find]( juce::Component& c ) -> ui::KickListBox* {
                if( auto* l = dynamic_cast<ui::KickListBox*>( &c ); l && l->isShowing() && l->isEnabled() )
                    return l;
                for( auto* child : c.getChildren() )
                    if( auto* hit = find( *child ) )
                        return hit;
                return nullptr;
            };
            if( auto* l = find( screen ) )
            {
                log( "opening list '" + l->getName() + "'" );
                l->knobClicked();
            }
            else
            {
                // Every pop-up goes through the same dialog code, so any content will do.
                log( "no list box showing; opening a dialog through mt::utils::showOptions" );
                auto list = std::make_unique<ui::KickList>( "Test", std::vector<juce::String> { "One", "Two", "Three" } );
                list->show( "TEST" );
                popupTestContent = std::move( list );
            }
        } );
        wait( 4 );
        windowShot( "popup_window" );
        s.push_back( [this, log] {
            auto* top = getTopLevelComponent();
            log( "window " + top->getLocalBounds().toString() + ", KICK screen in window " + top->getLocalArea( &screen, screen.getLocalBounds() ).toString() );
            for( auto* c : top->getChildren() )
                log( "  window child " + juce::String( typeid( *c ).name() ) + " bounds " + c->getBounds().toString() + ( c->isVisible() ? " visible" : "" ) );
            if( auto* modal = juce::Component::getCurrentlyModalComponent() )
            {
                log( "modal " + juce::String( typeid( *modal ).name() ) + " on desktop=" + juce::String( (int)modal->isOnDesktop() ) + " bounds in window "
                     + top->getLocalArea( modal, modal->getLocalBounds() ).toString() );
                for( auto* c : modal->getChildren() )
                    if( c->isVisible() )
                    {
                        auto r   = top->getLocalArea( c, c->getLocalBounds() );
                        auto hit = top->getComponentAt( r.getCentre() );
                        log( "  content " + juce::String( typeid( *c ).name() ) + " at " + r.toString() + "; a click at its centre reaches "
                             + ( hit ? juce::String( typeid( *hit ).name() ) : juce::String( "nothing" ) ) );
                    }
            }
            else
                log( "no modal component" );
            if( auto* modal = juce::Component::getCurrentlyModalComponent() )
                log( juce::String( "panel PLAY button clickable while open: " ) + ( modal->canModalEventBeSentToComponent( findButton( DCC::EN_PLAY ) ) ? "yes" : "no" )
                     + ", screen behind the dialog clickable: " + ( modal->canModalEventBeSentToComponent( &screen ) ? "yes" : "no" ) );
        } );
        // Drive the open list from the panel: jog +1 then ENTER should pick "Two" and close it.
        s.push_back( [this] { mainControl.getJog().turn( 1 ); } );
        wait( 1 );
        s.push_back( [this] { mainControl.getJog().pushCentre( true ); } );
        s.push_back( [this] { mainControl.getJog().pushCentre( false ); } );
        wait( 2 );
        s.push_back( [this, log] {
            auto* list = dynamic_cast<ui::KickList*>( popupTestContent.get() );
            log( "after jog +1 / ENTER: selected " + ( list ? list->getSelected() : juce::String( "?" ) ) + ", dialog "
                 + ( juce::Component::getCurrentlyModalComponent() ? "still open" : "closed" ) );
        } );
        s.push_back( [] { juce::Component::getCurrentlyModalComponent() ? juce::Component::getCurrentlyModalComponent()->exitModalState( 0 ) : void(); } );
        wait( 2 );
        s.push_back( [log] { log( "done" ); } );
        s.push_back( [] { juce::JUCEApplication::getInstance()->systemRequestedQuit(); } );
        startTimer( 400 );
    }

    void SimulatorBoard::startPickerTest()
    {
        auto& s    = selfTestSteps;
        auto  log  = []( juce::String text ) { SimulatorInterfaceImpl::log( "SELFTEST " + text ); };
        auto  wait = [&s]( int n ) { for( int i = 0; i < n; ++i ) s.push_back( [] {} ); };
        auto  click = [&s, this]( int code ) {
            s.push_back( [this, code] { if( auto* b = findButton( code ) ) b->press( true ); } );
            s.push_back( [this, code] { if( auto* b = findButton( code ) ) b->press( false ); } );
        };
        // All components of a type under Root, in tree order.
        auto findAll = []( juce::Component* root, auto* typeTag ) {
            using T = std::remove_pointer_t<decltype( typeTag )>;
            std::vector<T*>                        found;
            std::function<void( juce::Component* )> walk = [&]( juce::Component* c ) {
                if( auto* t = dynamic_cast<T*>( c ) )
                    found.push_back( t );
                for( auto* child : c->getChildren() )
                    walk( child );
            };
            if( root )
                walk( root );
            return found;
        };

        wait( 4 );
        s.push_back( [log] {
            auto exeDir = juce::File::getSpecialLocation( juce::File::currentExecutableFile ).getParentDirectory();
            auto source = exeDir.getChildFile( "projects/Trap Kit" );
            auto root   = juce::File::getSpecialLocation( juce::File::tempDirectory ).getChildFile( "kickem_picker" );
            root.deleteRecursively();
            auto       scratch = root.getChildFile( source.getFileName() );
            const bool ok      = source.copyDirectoryTo( scratch ) && mt::getAudioInterface().loadProject( scratch );
            log( juce::String( "loaded scratch Trap Kit" ) + ( ok ? "" : " (FAILED)" ) );
        } );
        wait( 8 );
        click( DCC::EN_MAINMODE );
        wait( 4 );
        s.push_back( [this, log, findAll] {
            // The track row's program picker: its icon button opens the "Select Program" dialog.
            auto pickers = findAll( &screen, (kick_ui::KickProgramSelectorWithDisplay*)nullptr );
            for( auto* p : pickers )
                if( p->isShowing() )
                    for( auto* b : findAll( p, (ui::KickImageButton*)nullptr ) )
                        if( b->getName() == "Program" )
                        {
                            log( "opening the track's program picker" );
                            b->triggerClick();
                            return;
                        }
            log( "no program picker showing (" + juce::String( (int)pickers.size() ) + " found)" );
        } );
        wait( 4 );
        s.push_back( [this, log, findAll] {
            auto* modal = juce::Component::getCurrentlyModalComponent();
            auto  rows  = findAll( modal, (kick_ui::ProgramSelectorDisplay*)nullptr );
            auto  table = findAll( modal, (ui::KickTableView*)nullptr );
            if( rows.size() < 5 || table.empty() )
            {
                log( "picker dialog not found" );
                return;
            }
            // Click the centre of the Plugin row (row 4) the way the canvas forwards a real click:
            // the event's position is in the row component, which the simulator draws scaled.
            auto* row = rows[4];
            auto  src = juce::Desktop::getInstance().getMainMouseSource();
            auto  pos = row->getLocalBounds().getCentre().toFloat();
            juce::MouseEvent e( src, pos, {}, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, row, row, juce::Time::getCurrentTime(), pos, juce::Time::getCurrentTime(), 1, false );
            static_cast<juce::Component*>( table[0] )->mouseDown( e );
            log( "clicked the Plugin row; table selected row " + juce::String( table[0]->getSelectedRow() ) + " (expected 4)" );
        } );
        wait( 3 );
        s.push_back( [log, findAll] {
            auto* modal = juce::Component::getCurrentlyModalComponent();
            for( auto* b : findAll( modal, (ui::KickToggleButton*)nullptr ) )
                if( b->getButtonText() == "+ New Plugin" && b->isShowing() )
                {
                    log( "picking '+ New Plugin'" );
                    b->triggerClick();
                    return;
                }
            log( "'+ New Plugin' option not shown" );
        } );
        wait( 2 );
        s.push_back( [log, findAll] {
            auto* modal = juce::Component::getCurrentlyModalComponent();
            for( auto* b : findAll( modal, (ui::KickImageButton*)nullptr ) )
                if( b->getName() == "Apply" && b->isShowing() )
                {
                    log( "Apply" );
                    b->triggerClick();
                    return;
                }
            log( "Apply button not found" );
        } );
        wait( 4 );
        s.push_back( [log] {
            auto& ae   = mt::getAudioInterface();
            auto* prog = ae.getProgramDataForTrack( ae.getSelectedSequence(), ae.getSelectedTrack() );
            log( "track now plays " + ( prog ? prog->name.get() + " (" + mt::toString( prog->type.get() ) + ")" : juce::String( "nothing" ) ) );
        } );
        s.push_back( [log] { log( "done" ); } );
        wait( 1 );
        s.push_back( [] { juce::JUCEApplication::getInstance()->systemRequestedQuit(); } );
        startTimer( 400 );
    }

    void SimulatorBoard::timerCallback()
    {
        if( selfTestIndex < selfTestSteps.size() )
            selfTestSteps[selfTestIndex++]();
        else
            stopTimer();
    }

    SimulatorBoard::~SimulatorBoard()
    {
        ShiftLatch::onChange = nullptr;
        juce::Desktop::getInstance().removeGlobalMouseListener( &mouseTrace );
        // KickHome is a process-wide singleton: detach it, don't let it be destroyed with the board.
        removeChildComponent( &screen );
    }

    void SimulatorBoard::handleOutput( const CPktMgr::HWMsg_t& m )
    {
        const auto& d = m.data;
        switch( m.pktType )
        {
            case CPktMgr::EN_PKT_TYPE_LED:
                // EN_LED_SET_SINGLE_LED / EN_LED_BLINK_SINGLE : { id, state, colour }
                if( ( m.pktCmd == DCC::EN_LED_SET_SINGLE_LED || m.pktCmd == DCC::EN_LED_BLINK_SINGLE ) && d.size() >= 2 )
                    setButtonLed( d[0], d[1] != DCC::StateOff );
                break;

            case CPktMgr::EN_PKT_TYPE_PAD:
                pads.applyPadPacket( m );
                break;

            case CPktMgr::EN_PKT_TYPE_SLIDER:
                if( ( m.pktCmd == DCC::EN_SLIDER_BAR_LED || m.pktCmd == DCC::EN_SLIDER_LEVEL_LED ) && !d.empty() )
                    slider.setLedLevel( d[0] );
                break;

            default: break;
        }
    }

    void SimulatorBoard::setButtonLed( int id, bool on )
    {
        if( id >= DCC::EN_BANK_A && id <= DCC::EN_BANK_H )
        {
            if( !on )
                return;
            // Bank LEDs are exclusive; E-H share the A-D buttons.
            auto banks = mainControl.getBankButtons();
            for( int i = 0; i < 4; ++i )
                banks[(size_t)i]->setLed( i == id % 4 );
            pads.setBankName( juce::String::charToString( (juce::juce_wchar)( 'A' + id ) ) );
            return;
        }

        if( on && ( id == DCC::EN_PLAY || id == DCC::EN_STOP || id == DCC::EN_PLAYSTART ) )
        {
            playBtn.setLed( id == DCC::EN_PLAY );
            stopBtn.setLed( id == DCC::EN_STOP );
            playStartBtn.setLed( id == DCC::EN_PLAYSTART );
            return;
        }

        if( on && ( id == DCC::EN_FULL_LEVEL || id == DCC::EN_HALF_LEVEL ) )
        {
            fullLevelBtn.setLed( true );
            return;
        }

        auto range = ledMap.equal_range( id );
        for( auto it = range.first; it != range.second; ++it )
            it->second->setLed( on );
    }

    void SimulatorBoard::paint( juce::Graphics& g )
    {
        g.fillAll( palette::body );

        // Brand
        g.setColour( palette::text );
        g.setFont( juce::Font( 15.0f, juce::Font::bold ) );
        g.drawText( "KLARK TEKNIK", juce::Rectangle<int>( 24, 14, 300, 20 ), juce::Justification::centredLeft );
        g.setFont( juce::Font( 30.0f, juce::Font::bold ) );
        g.drawText( "KICK", juce::Rectangle<int>( 24, 32, 200, 32 ), juce::Justification::centredLeft );
        g.setColour( palette::text.withAlpha( 0.5f ) );
        g.setFont( juce::Font( 12.0f ) );
        g.drawText( "KickEM simulator - controls send front-panel MCU packets", juce::Rectangle<int>( 230, 38, 370, 24 ), juce::Justification::centredRight );

        // Wells
        g.setColour( palette::well );
        g.fillRoundedRectangle( juce::Rectangle<int>( 20, 70, 580, 310 ).toFloat(), 8.0f );
        g.fillRoundedRectangle( juce::Rectangle<int>( 1656, 14, 132, 556 ).toFloat(), 8.0f );
        g.fillRoundedRectangle( juce::Rectangle<int>( 1656, 580, 132, 360 ).toFloat(), 8.0f );

        // Screen bezel
        g.setColour( juce::Colours::black );
        g.fillRoundedRectangle( screenRect.expanded( 8 ).toFloat(), 6.0f );

        g.setColour( palette::text );
        g.setFont( juce::Font( 13.0f, juce::Font::bold ) );
        g.drawText( "Q-LINK", juce::Rectangle<int>( 1656, 16, 132, 18 ), juce::Justification::centred );
        g.drawText( "TRANSPORT", juce::Rectangle<int>( 1656, 582, 132, 18 ), juce::Justification::centred );
    }

    void SimulatorBoard::resized()
    {
        screen.setBounds( screenRect );

        slider.setBounds( 30, 78, 80, 296 );
        masterKnob.setBounds( 120, 80, 150, 140 );
        recKnob.setBounds( 120, 230, 150, 140 );
        fullLevelBtn.setBounds( 300, 90, 280, 80 );
        levels16Btn.setBounds( 300, 185, 280, 80 );
        eraseBtn.setBounds( 300, 280, 280, 80 );
        pads.setBounds( 20, 390, 580, 550 );

        mainControl.setBounds( 620, 640, 1024, 300 );

        for( int i = 0; i < 4; ++i )
            qlinks[(size_t)i]->setBounds( 1664, 36 + i * 118, 116, 112 );
        linkBtn.setBounds( 1666, 512, 112, 50 );

        int y = 604;
        for( auto* b : { &recBtn, &overdubBtn, &stopBtn, &playBtn, &playStartBtn } )
        {
            b->setBounds( 1666, y, 112, 58 );
            y += 66;
        }
    }

    //==============================================================================
    // Simulator (scaling wrapper)
    //==============================================================================
    Simulator::Simulator()
    {
        addAndMakeVisible( board );
        setSize( SimulatorBoard::boardWidth, SimulatorBoard::boardHeight );
    }

    void Simulator::paint( juce::Graphics& g )
    {
        g.fillAll( juce::Colour( 0xff202020 ) );
    }

    void Simulator::resized()
    {
        const float bw    = (float)SimulatorBoard::boardWidth;
        const float bh    = (float)SimulatorBoard::boardHeight;
        const float scale = juce::jmax( 0.1f, juce::jmin( getWidth() / bw, getHeight() / bh ) );

        board.setBounds( 0, 0, SimulatorBoard::boardWidth, SimulatorBoard::boardHeight );
        board.setTransform( juce::AffineTransform::scale( scale ).translated( ( getWidth() - bw * scale ) * 0.5f, ( getHeight() - bh * scale ) * 0.5f ) );
    }
}
