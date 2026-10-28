#include <kick_ui/kick_ui.h>
#include <program_edit_ui/program_edit_ui.h>
#include <program_edit_ui/sources/instrument/kick_ModularEditor.h>
#include "kick_Simulator.h"
#include "kick_SimulatorInterfaceImpl.h"

namespace sim
{
    namespace
    {
        // Board coordinates of the hardware panel (see SimulatorBoard::boardWidth).
        // The firmware screen is 1024x600; it is shown scaled up to fill the bezel (screenRect).
        const juce::Rectangle<int> screenRect { 325, 75, 1214, 711 };
        const juce::Rectangle<int> bezelRect { 300, 52, 1266, 790 };
        const juce::Rectangle<int> cameraRect { 57, 52, 178, 77 };
        const juce::Rectangle<int> linkRect { 57, 641, 191, 156 };

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

        // Hardware look
        for( auto* k : { &masterKnob, &recKnob } )
        {
            k->setKnobDiameter( 114.0f );
            k->setLabelAbove( true );
            k->setShowMinMax( true );
        }
        for( auto& q : qlinks )
            q->setKnobDiameter( 104.0f );

        for( auto* b : { &fullLevelBtn, &levels16Btn, &eraseBtn, &linkBtn } )
            b->setStyle( SimButton::Style::key );
        fullLevelBtn.setLedColour( juce::Colour( 0xfff2eef8 ) );
        levels16Btn.setLedColour( juce::Colour( 0xffff5fd2 ) );
        eraseBtn.setLedColour( juce::Colour( 0xffff5fd2 ) );
        linkBtn.setLedColour( juce::Colour( 0xff3fd4ff ) );
        linkBtn.setCapSize( 104, 64 );

        recBtn.setStyle( SimButton::Style::round, SimButton::Icon::rec );
        overdubBtn.setStyle( SimButton::Style::round, SimButton::Icon::overdub );
        stopBtn.setStyle( SimButton::Style::round, SimButton::Icon::stop );
        playBtn.setStyle( SimButton::Style::round, SimButton::Icon::play );
        playStartBtn.setStyle( SimButton::Style::round, SimButton::Icon::playStart );
        for( auto* b : { &recBtn, &overdubBtn, &stopBtn, &playBtn, &playStartBtn } )
            b->setCapSize( 86, 86 );

        recBtn.setLedColour( juce::Colour( 0xffff2020 ) );
        overdubBtn.setLedColour( juce::Colour( 0xffff2020 ) );
        stopBtn.setLedColour( juce::Colour( 0xfff2eef8 ) );
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

        setWantsKeyboardFocus( true ); // space bar = PLAY / STOP, see keyPressed() and SpaceBar
        juce::Desktop::getInstance().addFocusChangeListener( &spaceBar );
        setSize( boardWidth, boardHeight ); // last: resized() needs every child

        if( juce::SystemStats::getEnvironmentVariable( "KICKEM_TRACE_MOUSE", {} ).isNotEmpty() )
            juce::Desktop::getInstance().addGlobalMouseListener( &mouseTrace );

        if( juce::SystemStats::getEnvironmentVariable( "KICKEM_SELFTEST", {} ).isNotEmpty() )
            startSelfTest();
        else
            installBundledContent();
    }

    void SimulatorBoard::togglePlayStop()
    {
        const int code = mt::getAudioInterface().isSequencePlaying() ? DCC::EN_STOP : DCC::EN_PLAY;
        if( auto* b = findButton( code ) )
        {
            b->press( true );
            b->press( false );
        }
    }

    bool SimulatorBoard::keyPressed( const juce::KeyPress& key )
    {
        if( key != juce::KeyPress::spaceKey )
            return false; // other keys: let the firmware handle them
        togglePlayStop();
        return true;
    }

    void SimulatorBoard::visibilityChanged()
    {
        // Take the keyboard once the window is up, so the space bar works without a click first.
        if( isShowing() )
            juce::MessageManager::callAsync( [safe = juce::Component::SafePointer<SimulatorBoard>( this )] {
                if( safe == nullptr || !safe->isShowing() )
                    return;
                if( !safe->hasKeyboardFocus( true ) )
                    safe->grabKeyboardFocus();
                safe->spaceBar.attach( juce::Component::getCurrentlyFocusedComponent() );
            } );
    }

    bool SimulatorBoard::SpaceBar::keyPressed( const juce::KeyPress& key, juce::Component* origin )
    {
        if( key != juce::KeyPress::spaceKey || dynamic_cast<juce::TextEditor*>( origin ) != nullptr )
            return false; // typing a name keeps its spaces
        board.togglePlayStop();
        return true;
    }

    void SimulatorBoard::SpaceBar::globalFocusChanged( juce::Component* focused ) { attach( focused ); }

    void SimulatorBoard::SpaceBar::attach( juce::Component* c )
    {
        if( attached != nullptr )
            attached->removeKeyListener( this );
        if( c == nullptr && board.isShowing() )
            c = board.getTopLevelComponent(); // nothing focused: keys go to the window itself
        attached = c;
        if( c != nullptr && c != &board ) // the board handles space in its own keyPressed()
            c->addKeyListener( this );
    }

    // The playable demo projects in <data>/content (Kick/content in the source tree) carry "@PROJECT_DIR@"
    // instead of their own folder in the sample paths, because the engine stores absolute paths. Copy each one
    // the user has not received yet into the projects folder and fill in the real path. On the very first start,
    // open "AwesomeKick Groove" so that PLAY sounds right away. Deleted demos are not installed again
    // (prefs/installed-content.txt remembers them).
    void SimulatorBoard::installBundledContent()
    {
        const auto content = getAppDirs( MTDemos ).getParentDirectory().getChildFile( "content" );
        if( !content.isDirectory() )
            return;
        auto              marker = getAppDirs( MTPrefs ).getChildFile( "installed-content.txt" );
        juce::StringArray installed;
        installed.addLines( marker.loadFileAsString() );
        installed.removeEmptyStrings();
        const bool firstStart = installed.isEmpty();
        auto       xmlEscape  = []( juce::String t ) {
            return t.replace( "&", "&amp;" ).replace( "\"", "&quot;" ).replace( "<", "&lt;" ).replace( ">", "&gt;" );
        };

        juce::File openFirst;
        for( const auto& src : content.findChildFiles( juce::File::findDirectories, false ) )
        {
            const auto name = src.getFileName();
            if( installed.contains( name ) )
                continue;
            auto dest = getAppDirs( MTProjects ).getChildFile( name );
            if( !dest.exists() )
            {
                auto prj = dest.getChildFile( name + mt::__projectFileSuffix );
                if( !src.copyDirectoryTo( dest )
                    || !prj.replaceWithText( prj.loadFileAsString().replace( "@PROJECT_DIR@", xmlEscape( dest.getFullPathName() ) ) ) )
                {
                    SimulatorInterfaceImpl::log( "content: could not install " + name );
                    continue;
                }
                SimulatorInterfaceImpl::log( "content: installed " + dest.getFullPathName() );
            }
            installed.add( name );
            if( firstStart && name == "AwesomeKick Groove" )
                openFirst = dest;
        }
        marker.replaceWithText( installed.joinIntoString( "\n" ) );
        if( openFirst != juce::File() )
            juce::Timer::callAfterDelay( 2500, [openFirst] { ui::LoadProjectAndAnimation::getInstance().startLoading( openFirst ); } );
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

        if(juce::SystemStats::getEnvironmentVariable("KICKEM_SELFTEST", {}) == "audit")
        {
            auto failed = std::make_shared<int>(0);
            auto check = [failed](bool ok, juce::String text) {
                if(!ok) ++*failed;
                SimulatorInterfaceImpl::log("SELFTEST " + juce::String(ok ? "PASS " : "FAIL ") + text);
            };
            s.push_back([check] {
                auto selection = std::make_unique<ui::KickListBox>("Audit", std::vector<juce::String>{"A", "B"});
                selection->setSelected(1, juce::dontSendNotification);
                check(selection->getSelectedIndex() == 1, "P1: message-thread selection applies synchronously");
                auto* raw = selection.get();
                std::thread producer([raw] { raw->setSelected(0, juce::dontSendNotification); });
                producer.join();
                selection.reset(); // queued selection must observe the deleted owner
                for(int i = 0; i < 100; ++i)
                {
                    auto control = std::make_unique<ui::KickCyclicStateControl>();
                    control->setStates({"A", "B"});
                    control->setNormalized(1.f);
                }
                ui::themeChanged();
                check(true, "P2: theme emission after 100 control destructions is safe");
                int count = 3;
                ui::KickTableViewProvider provider;
                provider.numRows = [&] { return count; };
                provider.cellComponent = [](int, juce::Component* previous) { return previous ? previous : new juce::Label(); };
                ui::KickTableView table("Audit", provider);
                table.setBounds(0, 0, 300, 200);
                table.refresh();
                count = 1; table.refresh();
                check(table.getRowComponents().size() == 1, "U8: shrinking the table destroys stale owned rows");
                count = 0; table.refresh();
                check(table.getRowComponents().isEmpty(), "U8: empty table releases every row");
                kick_ui::ProgramMixer programs;
                kick_ui::TrackMidiMixer midi;
                programs.knobCommand(1, 1);
                midi.knobCommand(1, 1);
                check(true, "P3: empty program and MIDI mixers accept knob input");
            });
            wait(3);
            s.push_back([failed] {
                SimulatorInterfaceImpl::log("SELFTEST done: " + juce::String(*failed) + " failures");
                juce::JUCEApplication::getInstance()->setApplicationReturnValue(*failed);
                juce::JUCEApplication::getInstance()->systemRequestedQuit();
            });
            startTimer(200);
            return;
        }

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
        if( juce::SystemStats::getEnvironmentVariable( "KICKEM_SELFTEST", {} ) == "resources" )
        {
            startResourcesTest();
            return;
        }
        if( juce::SystemStats::getEnvironmentVariable( "KICKEM_SELFTEST", {} ) == "record" )
        {
            startRecordTest();
            return;
        }
        if( juce::SystemStats::getEnvironmentVariable( "KICKEM_SELFTEST", {} ) == "modular" )
        {
            startModularTest();
            return;
        }
        if( juce::SystemStats::getEnvironmentVariable( "KICKEM_SELFTEST", {} ) == "makedemo" )
        {
            startMakeDemo();
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

        // Modular: pick it in the ENGINE box and a preset in the PRESET box, as a user would.
        auto listWith = [this]( juce::String Option ) -> ui::KickListBox* {
            std::function<ui::KickListBox*( juce::Component& )> find = [&find, &Option]( juce::Component& c ) -> ui::KickListBox* {
                if( auto* lb = dynamic_cast<ui::KickListBox*>( &c ); lb && lb->isShowing() )
                {
                    const auto options = lb->getListOptions();
                    if( std::find( options.begin(), options.end(), Option ) != options.end() )
                        return lb;
                }
                for( auto* child : c.getChildren() )
                    if( auto* hit = find( *child ) )
                        return hit;
                return nullptr;
            };
            return find( screen );
        };
        auto choose = [&s, listWith, log]( juce::String Option ) {
            s.push_back( [listWith, log, Option] {
                if( auto* lb = listWith( Option ) )
                {
                    lb->setSelected( Option, juce::sendNotification );
                    log( "chose " + Option );
                }
                else
                    log( "no list box offers " + Option );
            } );
        };
        auto report = [&s, log, instrument]() {
            s.push_back( [log, instrument] {
                if( auto* i = instrument() )
                    log( "engine " + i->getEngineName() + ", patch " + i->getPatchName() + ( i->getPatch().isValid() ? " (stored)" : " (default)" ) + ", " +
                         juce::String( (int)i->getParams().size() ) + " params, presets: " + i->getPresetNames().joinIntoString( ", " ) );
            } );
        };
        auto playPads = [&s, this, wait]() {
            for( int p = 0; p < 4; ++p )
            {
                s.push_back( [this, p] { pads.getPad( p ).hit( true, 100 ); } );
                wait( 1 );
                s.push_back( [this, p] { pads.getPad( p ).hit( false ); } );
            }
        };

        choose( "Modular" );
        wait( 3 );
        report();
        shot( "modular_edit_init" );
        playPads();
        choose( "FM Bell" );
        wait( 3 );
        report();
        shot( "modular_edit_fm_bell" );
        playPads();
        s.push_back( [log] { log( "Q-link P1 +40 (Macro 1)" ); } );
        s.push_back( [this] { qlinks[0]->turn( 40 ); } );
        wait( 2 );
        s.push_back( [log, instrument] {
            if( auto* i = instrument() )
                log( i->getParams()[0].name + "=" + juce::String( i->getParam( 0 ), 3 ) );
        } );
        playPads();
        shot( "modular_edit_macro" );
        s.push_back( [log] { log( "done" ); } );
        wait( 2 );
        s.push_back( [] { juce::JUCEApplication::getInstance()->systemRequestedQuit(); } );

        startTimer( 400 );
    }

    void SimulatorBoard::startModularTest()
    {
        auto& s     = selfTestSteps;
        auto  log   = []( juce::String text ) { SimulatorInterfaceImpl::log( "SELFTEST " + text ); };
        auto  wait  = [&s]( int n ) { for( int i = 0; i < n; ++i ) s.push_back( [] {} ); };
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
        // Finds a showing component anywhere on screen, popups included.
        auto findShowing = [this]( std::function<bool( juce::Component& )> Match ) -> juce::Component* {
            std::function<juce::Component*( juce::Component& )> find = [&find, &Match]( juce::Component& c ) -> juce::Component* {
                if( c.isShowing() && Match( c ) )
                    return &c;
                for( auto* child : c.getChildren() )
                    if( auto* hit = find( *child ) )
                        return hit;
                return nullptr;
            };
            return find( *getTopLevelComponent() );
        };
        auto editor = [findShowing]() { return dynamic_cast<kick_ui::ModularEditor*>( findShowing( []( juce::Component& c ) { return dynamic_cast<kick_ui::ModularEditor*>( &c ) != nullptr; } ) ); };
        auto tapButton = [&s, findShowing, log]( juce::String Text ) {
            s.push_back( [findShowing, log, Text] {
                auto* b = dynamic_cast<juce::Button*>( findShowing( [Text]( juce::Component& c ) {
                    auto* btn = dynamic_cast<juce::Button*>( &c );
                    return btn && btn->getButtonText() == Text;
                } ) );
                if( b )
                    b->triggerClick();
                log( ( b ? "tapped " : "no button " ) + Text );
            } );
        };
        auto describe = [&s, log, instrument]( juce::String When ) {
            s.push_back( [log, instrument, When] {
                auto* i = instrument();
                if( !i )
                    return;
                const auto                     patch = i->getPatch();
                const mt::modular::PatchEditor read( patch, nullptr );
                juce::StringArray              parts;
                for( const auto& m : patch )
                {
                    juce::String d = read.moduleLabel( m.getProperty( "id" ) ) + "@" + m.getProperty( "col" ).toString() + "," + m.getProperty( "row" ).toString();
                    if( m.hasProperty( "cutoff" ) )
                        d << " cutoff=" << juce::String( (float)m.getProperty( "cutoff" ), 1 );
                    for( const auto& r : m )
                        d << " [" << r.getProperty( "param" ).toString() << "<" << read.sourceLabel( r.getProperty( "source" ) ) << " "
                          << juce::String( (float)r.getProperty( r.hasType( "CABLE" ) ? "amount" : "depth" ), 2 ) << "]";
                    parts.add( d );
                }
                log( When + ": " + ( patch.isValid() ? patch.getProperty( "name" ).toString() : juce::String( "(default)" ) ) + " | " + parts.joinIntoString( " | " ) );
            } );
        };

        wait( 4 );
        s.push_back( [log] {
            auto exeDir  = juce::File::getSpecialLocation( juce::File::currentExecutableFile ).getParentDirectory();
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
            const auto trk  = ae.createTrack( seq, "Modular" );
            const auto prog = ae.createProgram( mt::MTProgramType::Plugin, "Modular 1" );
            ae.setProgramToTrack( prog, seq, trk );
            ae.setSelectedSequenceAndTrack( seq, trk );
            ae.setSelectedProgram( prog );
            if( auto* i = dynamic_cast<mt::MTInstrumentProgram*>( ae.getProgram( prog ) ) )
                i->setEngine( "Modular" );
            log( "created Modular program " + juce::String( prog ) );
        } );
        wait( 2 );
        click( DCC::EN_PROG_EDIT );
        wait( 4 );
        tapButton( "EDIT PATCH" );
        wait( 3 );
        s.push_back( [log, editor] { log( editor() ? "patch editor open" : "patch editor NOT open" ); } );
        describe( "start" );
        shot( "modular_editor_init" );

        // Select the filter and turn Q-link P2 (cutoff) down.
        s.push_back( [editor] { if( auto* e = editor() ) e->selectCell( 0, 1 ); } );
        wait( 1 );
        s.push_back( [this] { qlinks[1]->turn( -25 ); } );
        wait( 2 );
        describe( "after P2 -25 on the filter" );
        shot( "modular_editor_filter" );

        // Add an LFO in an empty cell through the module list, as a tap would.
        s.push_back( [editor] { if( auto* e = editor() ) e->selectCell( 1, 1 ); } );
        tapButton( "+ MODULE" );
        wait( 3 );
        shot( "modular_editor_picker" );
        tapButton( "LFO" );
        wait( 3 );
        describe( "after adding an LFO" );

        // Modulate the filter cutoff from it: + MOD, pick the knob, then the source.
        s.push_back( [editor] { if( auto* e = editor() ) e->selectCell( 0, 1 ); } );
        wait( 1 );
        tapButton( "+ MOD" );
        wait( 3 );
        tapButton( "Cutoff" );
        wait( 4 );
        tapButton( "LFO 1" );
        wait( 3 );
        s.push_back( [this] { qlinks[2]->turn( -20 ); } ); // the new modulation is third: P3, depth 0.5 -> 0.1
        wait( 2 );
        describe( "after modulating cutoff from the LFO" );
        shot( "modular_editor_mods" );

        // Move the LFO with the API a drag uses, play, then undo the move.
        s.push_back( [editor] {
            if( auto* e = editor() )
            {
                e->selectCell( 1, 1 );
                e->moveModule( 3, 2 );
            }
        } );
        wait( 2 );
        describe( "after moving the LFO" );
        click( DCC::EN_BANK_D );
        for( int p = 0; p < 4; ++p )
        {
            s.push_back( [this, p] { pads.getPad( p ).hit( true, 100 ); } );
            wait( 1 );
            s.push_back( [this, p] { pads.getPad( p ).hit( false ); } );
        }
        s.push_back( [] {
            mt::getAudioInterface().getUndoManager().beginNewTransaction();
            mt::getAudioInterface().performUndo();
        } );
        wait( 2 );
        describe( "after undo" );
        shot( "modular_editor_after_undo" );

        // Milestone 3: an effect, a cable, and saving a user preset.
        s.push_back( [editor] { if( auto* e = editor() ) e->selectCell( 0, kick_ui::ModularEditor::effectRow ); } );
        wait( 1 );
        tapButton( "+ EFFECT" );
        wait( 3 );
        tapButton( "Reverb" );
        wait( 3 );
        s.push_back( [this] { qlinks[3]->turn( 30 ); } ); // P4 = mix
        wait( 2 );
        describe( "after adding a reverb" );
        shot( "modular_editor_effect" );

        s.push_back( [editor] { if( auto* e = editor() ) e->selectCell( 2, 0 ); } );
        tapButton( "+ MODULE" );
        wait( 3 );
        tapButton( "Oscillator" );
        wait( 3 );
        s.push_back( [editor] { if( auto* e = editor() ) e->selectCell( 0, 0 ); } );
        wait( 1 );
        tapButton( "IN (0)" );
        wait( 1 );
        tapButton( "+ CABLE" );
        wait( 3 );
        tapButton( "Oscillator 2" );
        wait( 3 );
        s.push_back( [this] { qlinks[0]->turn( 20 ); } ); // amount 0.5 -> 0.9 (2% of -1..1 per detent)
        wait( 2 );
        describe( "after cabling Oscillator 2 into Oscillator 1" );
        shot( "modular_editor_cable" );
        click( DCC::EN_BANK_D );
        for( int p = 0; p < 4; ++p )
        {
            s.push_back( [this, p] { pads.getPad( p ).hit( true, 100 ); } );
            wait( 1 );
            s.push_back( [this, p] { pads.getPad( p ).hit( false ); } );
        }

        tapButton( "SAVE" );
        wait( 3 );
        shot( "modular_editor_save" );
        tapButton( "Apply" );
        wait( 3 );
        s.push_back( [log, instrument] {
            if( auto* i = instrument() )
                log( "after SAVE: patch " + i->getPatchName() + ", presets: " + i->getPresetNames().joinIntoString( ", " ) + " | macro 1 = " + i->getParamName( 0 ) );
        } );

        // Round 4: legato with glide, pressure, live activity, moving an effect, deleting a preset.
        tapButton( "POLY 8" );
        wait( 3 );
        tapButton( "Legato" );
        wait( 4 );
        tapButton( "100 ms" );
        wait( 3 );
        s.push_back( [log, instrument] {
            if( auto* i = instrument() )
            {
                const mt::modular::PatchEditor read( i->getPatch(), nullptr );
                log( "play mode " + juce::String( read.getPlayMode() ) + ", glide " + juce::String( read.getGlide(), 3 ) + " s" );
            }
        } );
        s.push_back( [editor] {
            if( auto* e = editor() )
            {
                e->selectCell( 0, kick_ui::ModularEditor::effectRow );
                e->moveEffect( 2 );
            }
        } );
        wait( 2 );
        describe( "after moving the reverb to FX 3" );
        s.push_back( [this] { pads.getPad( 0 ).hit( true, 110 ); } );
        wait( 2 );
        s.push_back( [this] { pads.getPad( 0 ).press( 100 ); } );
        wait( 3 );
        s.push_back( [log, instrument] {
            if( auto* i = instrument() )
            {
                juce::StringArray parts;
                for( const auto& m : i->getPatch() )
                    parts.add( m.getProperty( "type" ).toString() + "=" + juce::String( i->getActivity( m.getProperty( "id" ) ), 2 ) );
                log( "activity while held: " + parts.joinIntoString( " " ) );
            }
        } );
        shot( "modular_editor_activity" );
        s.push_back( [this] { pads.getPad( 0 ).hit( false ); } );
        wait( 2 );

        tapButton( "SAVE" );
        wait( 3 );
        tapButton( "Delete a user preset" );
        wait( 4 );
        tapButton( "Init 2" );
        wait( 3 );
        s.push_back( [log, instrument] {
            if( auto* i = instrument() )
                log( "after delete: presets " + i->getPresetNames().joinIntoString( ", " ) );
        } );

        // Wave 5: a Formant module from the list, and the arpeggiator set up through ARP.
        s.push_back( [editor] { if( auto* e = editor() ) e->selectCell( 3, 0 ); } );
        tapButton( "+ MODULE" );
        wait( 3 );
        tapButton( "Formant" );
        wait( 3 );
        tapButton( "ARP OFF" );
        wait( 3 );
        tapButton( "Up" );
        wait( 4 );
        tapButton( "1/16" );
        wait( 4 );
        tapButton( "2" );
        wait( 4 );
        tapButton( "50%" );
        wait( 3 );
        s.push_back( [log, instrument] {
            if( auto* i = instrument() )
            {
                const mt::modular::PatchEditor read( i->getPatch(), nullptr );
                log( "arp mode " + juce::String( read.getArp() ) + ", rate " + juce::String( read.getArpRate() ) + ", octaves " + juce::String( read.getArpOctaves() ) + ", gate " +
                     juce::String( read.getArpGate(), 2 ) + " | modules: " + juce::String( i->getPatch().getNumChildren() ) );
            }
        } );
        for( int p : { 0, 2, 4 } ) // a chord: three pads together
            s.push_back( [this, p] { pads.getPad( p ).hit( true, 100 ); } );
        wait( 3 );
        shot( "modular_editor_arp" );
        wait( 2 );
        for( int p : { 0, 2, 4 } )
            s.push_back( [this, p] { pads.getPad( p ).hit( false ); } );
        wait( 2 );

        tapButton( "< BACK" );
        wait( 3 );
        s.push_back( [log, instrument] {
            if( auto* i = instrument() )
                log( "back on the instrument page, preset box shows: " + i->getPatchName() );
        } );
        shot( "modular_editor_closed" );
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

    void SimulatorBoard::startResourcesTest()
    {
        auto& s    = selfTestSteps;
        auto  log  = []( juce::String text ) { SimulatorInterfaceImpl::log( "SELFTEST " + text ); };
        auto  wait = [&s]( int n ) { for( int i = 0; i < n; ++i ) s.push_back( [] {} ); };
        auto  click = [&s, this]( int code ) {
            s.push_back( [this, code] { if( auto* b = findButton( code ) ) b->press( true ); } );
            s.push_back( [this, code] { if( auto* b = findButton( code ) ) b->press( false ); } );
        };

        wait( 4 );
        s.push_back( [log] {
            auto exeDir = juce::File::getSpecialLocation( juce::File::currentExecutableFile ).getParentDirectory();
            auto source = exeDir.getChildFile( "projects/Vintage Soul Kit" );
            auto root   = juce::File::getSpecialLocation( juce::File::tempDirectory ).getChildFile( "kickem_resources" );
            root.deleteRecursively();
            auto       scratch = root.getChildFile( source.getFileName() );
            const bool ok      = source.copyDirectoryTo( scratch ) && mt::getAudioInterface().loadProject( scratch );
            log( juce::String( "loaded scratch kit" ) + ( ok ? "" : " (FAILED)" ) );
        } );
        wait( 8 );
        click( DCC::EN_MAINMODE );
        wait( 3 );
        click( DCC::EN_PLAY );
        wait( 2 );
        // GRID (SHIFT + MAIN): its header has the chip button; Main's top bar doesn't.
        s.push_back( [this] { if( auto* b = findButton( DCC::EN_MAINMODE ) ) b->press( true, true ); } );
        s.push_back( [this] { if( auto* b = findButton( DCC::EN_MAINMODE ) ) b->press( false ); } );
        wait( 3 );
        s.push_back( [this, log] {
            // Open the page the way the header's chip button does.
            std::function<ui::KickImageButton*( juce::Component& )> find = [&find]( juce::Component& c ) -> ui::KickImageButton* {
                if( auto* b = dynamic_cast<ui::KickImageButton*>( &c ); b && b->getName() == "Resources" && b->isShowing() )
                    return b;
                for( auto* child : c.getChildren() )
                    if( auto* hit = find( *child ) )
                        return hit;
                return nullptr;
            };
            if( auto* b = find( screen ) )
            {
                log( "opening System Resources" );
                b->triggerClick();
            }
            else
                log( "Resources button not found" );
        } );
        // The page refreshes once a second; let it take a few readings while the kit plays.
        wait( 10 );
        s.push_back( [this, log] {
            std::function<void( juce::Component& )> dump = [&]( juce::Component& c ) {
                if( auto* sl = dynamic_cast<ui::KickSlider*>( &c ); sl && sl->isShowing() )
                    log( "  " + sl->getName() + " = " + juce::String( sl->getValue(), 1 ) + " %" );
                for( auto* child : c.getChildren() )
                    dump( *child );
            };
            if( auto* modal = juce::Component::getCurrentlyModalComponent() )
                dump( *modal );
            log( juce::String( "playing = " ) + ( mt::getAudioInterface().isSequencePlaying() ? "yes" : "no" ) );
            const auto dir = juce::SystemStats::getEnvironmentVariable( "KICKEM_SHOT", {} );
            if( dir.isNotEmpty() )
                if( auto out = juce::File( dir ).getChildFile( "system_resources.png" ).createOutputStream() )
                    juce::PNGImageFormat().writeImageToStream( screen.createComponentSnapshot( screen.getLocalBounds() ), *out );
        } );
        // Leave time for an outside reading of this process before quitting.
        wait( 8 );
        click( DCC::EN_STOP );
        s.push_back( [] { juce::Component::getCurrentlyModalComponent() ? juce::Component::getCurrentlyModalComponent()->exitModalState( 0 ) : void(); } );
        s.push_back( [log] { log( "done" ); } );
        wait( 1 );
        s.push_back( [] { juce::JUCEApplication::getInstance()->systemRequestedQuit(); } );
        startTimer( 400 );
    }

    // Writes the electronic "AwesomeKick Groove" kit: 8 drum sounds synthesized here (no third-party samples).
    static bool writeSynthKit( const juce::File& dir )
    {
        static const char* sounds[] = { "Kick", "Snare", "Clap", "Closed Hat", "Open Hat", "Low Tom", "High Tom", "Rim" };
        dir.createDirectory();
        // Synthesize the kit: 44.1 kHz, mono, 16 bit. Fixed seed, so every run gives the same sounds.
        constexpr double sr = 44100.0;
        juce::Random     rnd( 0x4b69636b );
        auto envelope = []( double t, double decay ) { return std::exp( -t / decay ); };
        auto render   = [&]( int sound ) {
            const int                 len = (int)( sr * ( sound == 0 ? 0.6 : sound == 4 ? 0.5 : 0.35 ) );
            juce::AudioBuffer<float>  buf( 1, len );
            double                    phase = 0.0, hp = 0.0, lastNoise = 0.0;
            for( int i = 0; i < len; ++i )
            {
                const double t     = i / sr;
                const double noise = rnd.nextDouble() * 2.0 - 1.0;
                hp                 = 0.85 * ( hp + noise - lastNoise ); // one-pole high-pass for hats/claps
                lastNoise          = noise;
                double v           = 0.0;
                auto   sweep       = [&]( double f0, double f1, double tau ) {
                    phase += juce::MathConstants<double>::twoPi * ( f1 + ( f0 - f1 ) * std::exp( -t / tau ) ) / sr;
                    return std::sin( phase );
                };
                switch( sound )
                {
                    case 0: v = sweep( 160.0, 45.0, 0.04 ) * envelope( t, 0.25 ) + noise * envelope( t, 0.003 ) * 0.3; break;
                    case 1: v = sweep( 220.0, 180.0, 0.02 ) * envelope( t, 0.06 ) * 0.6 + hp * envelope( t, 0.12 ) * 0.7; break;
                    case 2:
                    {
                        const double burst = std::fmod( t, 0.011 ) < 0.004 && t < 0.035 ? 1.0 : 0.0;
                        v                  = hp * ( t < 0.035 ? burst : envelope( t - 0.035, 0.08 ) ) * 0.9;
                        break;
                    }
                    case 3: v = hp * envelope( t, 0.035 ) * 0.6; break;
                    case 4: v = hp * envelope( t, 0.25 ) * 0.5; break;
                    case 5: v = sweep( 140.0, 90.0, 0.08 ) * envelope( t, 0.2 ) * 0.9; break;
                    case 6: v = sweep( 230.0, 160.0, 0.06 ) * envelope( t, 0.16 ) * 0.9; break;
                    case 7: v = ( sweep( 900.0, 800.0, 0.01 ) * 0.6 + hp * 0.4 ) * envelope( t, 0.015 ); break;
                }
                const double fade = juce::jmin( 1.0, ( len - i ) / ( sr * 0.005 ) ); // no click at the end
                buf.setSample( 0, i, (float)( v * fade ) );
            }
            if( const auto peak = buf.getMagnitude( 0, len ); peak > 0.0f )
                buf.applyGain( juce::Decibels::decibelsToGain( sound == 3 || sound == 4 ? -6.0f : -1.0f ) / peak ); // hats a bit quieter
            return buf;
        };
        for( int i = 0; i < (int)std::size( sounds ); ++i )
        {
            auto file = dir.getChildFile( juce::String( i + 1 ).paddedLeft( '0', 2 ) + " " + sounds[i] + ".wav" );
            file.deleteFile();
            auto buf = render( i );
            if( auto out = file.createOutputStream() )
            {
                std::unique_ptr<juce::OutputStream> stream( out.release() );
                if( auto writer = std::unique_ptr<juce::AudioFormatWriter>( juce::WavAudioFormat().createWriterFor( stream.get(), sr, 1, 16, {}, 0 ) ) )
                {
                    stream.release(); // the writer owns it now
                    writer->writeFromAudioSampleBuffer( buf, 0, buf.getNumSamples() );
                }
            }
        }
return dir.getNumberOfChildFiles( juce::File::findFiles, "*.wav" ) == (int)std::size( sounds );
    }

    void SimulatorBoard::startMakeDemo()
    {
        // KICKEM_SELFTEST=makedemo builds the playable demo projects that ship in Kick/content (the factory demos
        // have no sequences or samples, so PLAY does nothing there):
        //   "AwesomeKick Groove" - electronic kit synthesized here, 2-bar groove on pads 1-8
        //   "Rusty Kit"      - 16 acoustic one-shots from KICKEM_MAKEDEMO_KIT (made by scripts/build-cc0-kit.py, CC0)
        // Projects go to KICKEM_MAKEDEMO_OUT (default: the projects folder), their samples into <project>/Samples.
        // Existing projects are left alone. Then the app quits.
        auto& s    = selfTestSteps;
        auto  log  = []( juce::String text ) { SimulatorInterfaceImpl::log( "SELFTEST " + text ); };
        auto  wait = [&s]( int n ) { for( int i = 0; i < n; ++i ) s.push_back( [] {} ); };

        const auto outEnv = juce::SystemStats::getEnvironmentVariable( "KICKEM_MAKEDEMO_OUT", {} );
        const auto kitEnv = juce::SystemStats::getEnvironmentVariable( "KICKEM_MAKEDEMO_KIT", {} );
        const auto outDir = outEnv.isNotEmpty() ? juce::File( outEnv ) : getAppDirs( MTProjects );

        struct Demo
        {
            juce::String                                    name, program;
            int                                             bpm;
            std::vector<std::pair<int, const char*>>        pattern; // pad (0-15) -> 32 steps: 'X' accent, 'x' normal, 'o' soft
            std::function<bool( const juce::File& )>        writeSamples;
        };
        std::vector<Demo> demos;
        demos.push_back( { "AwesomeKick Groove", "Community Kit", 104,
                           { { 0, "X.....x.X.x.....X......xX.....x." },   // Kick
                             { 1, "....X.......X.......X.......X..o" },   // Snare
                             { 2, "............................x.x." },   // Clap
                             { 3, "x.o.x.o.x.o.x...x.o.x...x.o.x.o." },   // Closed Hat
                             { 4, "..............x.......x........." },   // Open Hat
                             { 5, "..........................x....." },   // Low Tom
                             { 6, ".........................x......" },   // High Tom
                             { 7, "...o.......o.......o............" } }, // Rim
                           writeSynthKit } );
        if( kitEnv.isNotEmpty() )
            demos.push_back( { "Rusty Kit", "Rusty Kit", 96,
                               { { 0, "X.......x.x.....X.......x.x....." },   // Kick
                                 { 1, "....X.......X..o....X..o...x...." },   // Snare
                                 { 4, "x.o.x.o.x.o.x.o.x.o.x.o.x......." },   // Hihat closed
                                 { 5, "............................o..." },   // Hihat pedal
                                 { 6, "..........................x....." },   // Hihat open
                                 { 8, "............................x..." },   // Toms: fill at the end of bar 2
                                 { 9, ".............................x.." },
                                 { 10, "..............................x." },
                                 { 11, "...............................X" },
                                 { 13, "X..............................." } }, // Crash
                               [kitEnv]( const juce::File& dir ) {
                                   dir.createDirectory();
                                   bool ok = true;
                                   for( const auto& f : juce::File( kitEnv ).findChildFiles( juce::File::findFiles, false, "*.wav" ) )
                                       ok = f.copyFileTo( dir.getChildFile( f.getFileName() ) ) && ok;
                                   return ok && dir.getNumberOfChildFiles( juce::File::findFiles, "*.wav" ) > 0;
                               } } );
        else
            log( "makedemo: KICKEM_MAKEDEMO_KIT not set, skipping \"Rusty Kit\"" );

        wait( 4 );
        for( const auto& demo : demos )
        {
            auto project = std::make_shared<juce::File>( outDir.getChildFile( demo.name ) );
            auto build   = std::make_shared<bool>( false );
            s.push_back( [log, demo, project, build] {
                if( project->exists() )
                {
                    log( "makedemo: " + project->getFullPathName() + " already exists, nothing changed (delete it to rebuild)" );
                    return;
                }
                project->createDirectory();
                if( !demo.writeSamples( project->getChildFile( "Samples" ) ) )
                {
                    log( "makedemo: could not write the samples of " + demo.name );
                    return;
                }
                // The engine opens <folder>/<folder name>.kickPrj: start from the empty template.
                auto templ = getAppDirs( MTTemp ).getChildFile( "Template 01" );
                templ.getChildFile( "Template 01.png" ).copyFileTo( project->getChildFile( demo.name + ".png" ) );
                *build = templ.getChildFile( "Template 01.kickPrj" ).copyFileTo( project->getChildFile( demo.name + ".kickPrj" ) )
                         && mt::getAudioInterface().loadProject( *project );
                log( "makedemo: created " + project->getFullPathName() + ( *build ? "" : " (LOAD FAILED)" ) );
            } );
            wait( 8 );
            s.push_back( [log, demo, project, build] {
                if( !*build )
                    return;
                auto&      ae  = mt::getAudioInterface();
                const int  seq = ae.getNumSequences() ? ae.getSelectedSequence() : ae.createSequence( "Groove" );
                const int  trk = ae.getNumTracks( seq ) ? 0 : ae.createTrack( seq, "Drums" );
                const int  prg = ae.createProgram( mt::MTProgramType::Drum, demo.program );
                auto samples   = project->getChildFile( "Samples" ).findChildFiles( juce::File::findFiles, false, "*.wav" );
                samples.sort();
                for( int i = 0; i < juce::jmin( 16, samples.size() ); ++i )
                {
                    const auto path = samples[i].getFullPathName();
                    ae.addAsset( path, mt::AssetTypes::Samples );
                    ae.addSampleToProgram( prg, i, path ); // bank A pad n+1 = note n
                }
                ae.setProgramToTrack( prg, seq, trk );
                ae.setMasterBPM( demo.bpm ); // the template follows the master tempo
                ae.setSequenceBPM( seq, demo.bpm );
                ae.setSequenceLength( seq, 2 );
                ae.setSequenceLoop( seq, true );
                ae.setSelectedSequenceAndTrack( seq, trk );
                ae.setSelectedProgram( prg );

                auto*     tm    = ae.getTempoMgr( seq );
                const int step  = ( tm ? tm->getTicksPerBeat() : 960 ) / 4;
                int       notes = 0;
                for( const auto& [pad, row] : demo.pattern )
                    for( int i = 0; row[i] != 0 && i < 32; ++i )
                        if( const char c = row[i]; c != '.' )
                        {
                            ae.addOrModifyNotesInTrack( seq, trk, pad, i * step, step / 2, c == 'X' ? 127 : c == 'x' ? 100 : 70 );
                            ++notes;
                        }
                const bool saved = ae.saveProject();
                log( "makedemo: " + demo.name + ": program with " + juce::String( ae.getNumProgramSamples( prg ) ) + " samples, "
                     + juce::String( notes ) + " notes at " + juce::String( step ) + " ticks/step, saved: " + ( saved ? "yes" : "NO" ) );
            } );
            wait( 4 );
        }
        s.push_back( [] { juce::JUCEApplication::getInstance()->systemRequestedQuit(); } );
        startTimer( 400 );
    }

    void SimulatorBoard::startRecordTest()
    {
        auto& s    = selfTestSteps;
        auto  log  = []( juce::String text ) { SimulatorInterfaceImpl::log( "SELFTEST " + text ); };
        auto  wait = [&s]( int n ) { for( int i = 0; i < n; ++i ) s.push_back( [] {} ); };
        auto  click = [&s, this]( int code ) {
            s.push_back( [this, code] { if( auto* b = findButton( code ) ) b->press( true ); } );
            s.push_back( [this, code] { if( auto* b = findButton( code ) ) b->press( false ); } );
        };
        // Starts of the selected track's notes on Note.
        auto starts = []( int Note ) {
            auto&                    ae = mt::getAudioInterface();
            std::vector<mt::MTNote>  notes;
            std::vector<int>         result;
            ae.getNotesInRange( ae.getSelected(), notes, mt::NotesInRange( Note, Note ) );
            for( auto& n : notes )
                result.push_back( n.start.get() );
            std::sort( result.begin(), result.end() );
            return result;
        };
        static std::vector<int> before; // shared between steps
        constexpr int           pad  = 2; // bank A pad 3 = note 2
        constexpr int           note = 2;

        wait( 4 );
        s.push_back( [log] {
            auto exeDir = juce::File::getSpecialLocation( juce::File::currentExecutableFile ).getParentDirectory();
            auto source = exeDir.getChildFile( "projects/Trap Kit" );
            auto root   = juce::File::getSpecialLocation( juce::File::tempDirectory ).getChildFile( "kickem_record" );
            root.deleteRecursively();
            auto       scratch = root.getChildFile( source.getFileName() );
            const bool ok      = source.copyDirectoryTo( scratch ) && mt::getAudioInterface().loadProject( scratch );
            log( juce::String( "loaded scratch Trap Kit" ) + ( ok ? "" : " (FAILED)" ) );
        } );
        wait( 8 );
        click( DCC::EN_MAINMODE );
        wait( 3 );
        s.push_back( [log, starts] {
            auto& ae = mt::getAudioInterface();
            before   = starts( note );
            auto* tm = ae.getTempoMgr( ae.getSelectedSequence() );
            log( "note " + juce::String( note ) + " has " + juce::String( (int)before.size() ) + " events before recording; grid "
                 + juce::String( tm ? tm->getTicksPerQuantization() : 0 ) + " ticks, " + juce::String( tm ? tm->getBPM() : 0 ) + " BPM" );
        } );
        s.push_back( [log] { log( "press REC" ); } );
        click( DCC::EN_RECORD );
        wait( 1 );
        s.push_back( [this, log] {
            auto& ae = mt::getAudioInterface();
            log( juce::String( "recording = " ) + ( ae.isSequenceRecording() ? "yes" : "no" ) + ", playing = " + ( ae.isSequencePlaying() ? "yes" : "no" )
                 + ", REC LED = " + ( findButton( DCC::EN_RECORD ) && findButton( DCC::EN_RECORD )->isLedOn() ? "on" : "off" ) );
        } );
        // Five hits, a little under a second apart (timer steps are 400 ms), held ~200-400 ms.
        for( int i = 0; i < 5; ++i )
        {
            s.push_back( [this] { pads.getPad( pad ).hit( true, 90 + 5 * 0 ); } );
            s.push_back( [this] { pads.getPad( pad ).hit( false ); } );
        }
        wait( 2 );
        s.push_back( [log] { log( "press STOP" ); } );
        click( DCC::EN_STOP );
        wait( 2 );
        s.push_back( [this, log, starts] {
            auto&      ae    = mt::getAudioInterface();
            const auto after = starts( note );
            auto*      tm    = ae.getTempoMgr( ae.getSelectedSequence() );
            const int  grid  = tm ? juce::jmax( 1, tm->getTicksPerQuantization() ) : 1;
            juce::StringArray added;
            int               offGrid = 0;
            for( auto t : after )
                if( std::find( before.begin(), before.end(), t ) == before.end() )
                {
                    added.add( juce::String( t ) );
                    offGrid += ( t % grid ) != 0;
                }
            log( "recording = " + juce::String( ae.isSequenceRecording() ? "yes" : "no" ) + ", REC LED = "
                 + ( findButton( DCC::EN_RECORD ) && findButton( DCC::EN_RECORD )->isLedOn() ? "on" : "off" ) );
            log( "note " + juce::String( note ) + " now has " + juce::String( (int)after.size() ) + " events; new at ticks [" + added.joinIntoString( ", " ) + "], "
                 + juce::String( offGrid ) + " off the grid" );
            ae.getUndoManager().undo();
            log( "after one undo: " + juce::String( (int)starts( note ).size() ) + " events (expected " + juce::String( (int)before.size() ) + ")" );
        } );

        // Sampler: panel REC arms, recording starts when the input crosses the threshold, REC again stops
        // (or cancels while still waiting for signal).
        auto inputLevel = [] {
            float peak = -100.f;
            for( auto& m : mt::getAudioInterface().getRecorderInputMeter() )
                peak = juce::jmax( peak, m.peak );
            return peak;
        };
        auto samplerState = [log, inputLevel]( juce::String when ) {
            auto& ae = mt::getAudioInterface();
            log( "sampler " + when + ": running = " + juce::String( ae.isRecorderRunning() ? "yes" : "no" ) + ", progress " + juce::String( ae.getRecorderProgress(), 3 )
                 + ", input peak " + juce::String( inputLevel(), 1 ) + " dB, threshold " + juce::String( ae.getRecorderThresholdDB(), 1 ) + " dB" );
        };
        s.push_back( [log] { log( "press SAMPLER, then REC" ); } );
        click( DCC::EN_SAMPLER );
        wait( 3 );
        click( DCC::EN_RECORD );
        wait( 5 );
        s.push_back( [samplerState] { samplerState( "2 s after REC" ); } );
        s.push_back( [log, inputLevel] {
            auto& ae = mt::getAudioInterface();
            if( ae.getRecorderProgress() <= 0.0f )
                log( "nothing above the threshold yet; REC again should cancel the arm" );
        } );
        click( DCC::EN_RECORD );
        wait( 2 );
        s.push_back( [samplerState] { samplerState( "after second REC" ); } );
        // If the first take never triggered (quiet room), retry with the threshold at its minimum.
        s.push_back( [log] {
            auto& ae = mt::getAudioInterface();
            if( ae.getRecorderProgress() <= 0.0f )
            {
                log( "retrying with the threshold at -100 dB" );
                ae.setRecorderThresholdDB( -100.f );
            }
        } );
        click( DCC::EN_RECORD );
        wait( 5 );
        s.push_back( [samplerState] { samplerState( "2 s into retry" ); } );
        click( DCC::EN_RECORD );
        wait( 2 );
        s.push_back( [samplerState] { samplerState( "stopped" ); } );
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
        juce::Desktop::getInstance().removeFocusChangeListener( &spaceBar );
        if( spaceBar.attached != nullptr )
            spaceBar.attached->removeKeyListener( &spaceBar );
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
        // Panel body
        auto body = getLocalBounds().toFloat().reduced( 2.0f );
        g.setGradientFill( juce::ColourGradient( palette::panel.brighter( 0.08f ), 0.0f, 0.0f, palette::panel.darker( 0.15f ), body.getRight(), body.getBottom(), false ) );
        g.fillRoundedRectangle( body, 42.0f );
        g.setColour( juce::Colour( 0xff3a3a3e ) );
        g.drawRoundedRectangle( body, 42.0f, 2.0f );

        // Camera window (AI camera) - decorative
        g.setColour( juce::Colour( 0xff0d0d0f ) );
        g.fillRoundedRectangle( cameraRect.toFloat(), 10.0f );
        g.setColour( juce::Colour( 0xff34343a ) );
        g.drawRoundedRectangle( cameraRect.toFloat(), 10.0f, 1.5f );
        g.setColour( juce::Colour( 0xff6e5b3a ) );
        g.fillRect( juce::Rectangle<float>( 18.0f, 22.0f ).withCentre( { 128.0f, 92.0f } ) );
        auto lens = juce::Rectangle<float>( 40.0f, 40.0f ).withCentre( { 174.0f, 92.0f } );
        g.setGradientFill( juce::ColourGradient( juce::Colour( 0xff3b3f6a ), lens.getX(), lens.getY(), juce::Colour( 0xff07070a ), lens.getRight(), lens.getBottom(), false ) );
        g.fillEllipse( lens );
        g.setColour( juce::Colours::white.withAlpha( 0.5f ) );
        g.fillEllipse( juce::Rectangle<float>( 8.0f, 8.0f ).withCentre( { 167.0f, 85.0f } ) );

        // LINK section: recess with four status LEDs
        g.setColour( juce::Colour( 0xff151517 ) );
        g.fillRoundedRectangle( linkRect.toFloat(), 12.0f );
        g.setColour( juce::Colour( 0xff3a3a3f ) );
        g.drawRoundedRectangle( linkRect.toFloat(), 12.0f, 1.5f );
        for( int x : { 91, 132, 171, 212 } )
        {
            g.setColour( juce::Colour( 0xff050505 ) );
            g.fillRect( juce::Rectangle<float>( 22.0f, 16.0f ).withCentre( { (float)x, 667.0f } ) );
        }

        // Screen bezel
        g.setColour( juce::Colour( 0xff0a0a0b ) );
        g.fillRoundedRectangle( bezelRect.toFloat(), 14.0f );
        g.setColour( juce::Colour( 0xff2c2c30 ) );
        g.drawRoundedRectangle( bezelRect.toFloat(), 14.0f, 2.0f );
        g.setColour( juce::Colours::black );
        g.fillRect( screenRect.expanded( 4 ) );

        // Community name under the screen (no manufacturer logo)
        g.setColour( palette::legend.withAlpha( 0.75f ) );
        g.setFont( juce::Font( 34.0f, juce::Font::bold ) );
        g.drawText( "AwesomeKick", juce::Rectangle<int>( bezelRect.getX(), screenRect.getBottom() + 6, bezelRect.getWidth(), bezelRect.getBottom() - screenRect.getBottom() - 10 ),
                    juce::Justification::centred );
    }

    void SimulatorBoard::resized()
    {
        // All positions in board units, measured from the hardware panel.
        screen.setBounds( 0, 0, 1024, 600 );
        screen.setTransform( juce::AffineTransform::scale( (float)screenRect.getWidth() / 1024.0f )
                                 .translated( (float)screenRect.getX(), (float)screenRect.getY() ) );

        // Upper left: MASTER and REC pots, LINK
        masterKnob.setBounds( 127 - 90, 279 - 94, 180, 196 );
        recKnob.setBounds( 127 - 90, 494 - 94, 180, 196 );
        linkBtn.setBounds( 155 - 60, 732 - 33, 120, 68 );

        // Lower left: touch strip, FULL LEVEL / 16 LEVELS / ERASE
        slider.setBounds( 36, 905, 161, 383 );
        auto key = []( SimButton& b, int cx, int cy ) { b.setBounds( cx - 62, cy - 36, 124, 118 ); };
        key( fullLevelBtn, 119, 1396 );
        key( levels16Btn, 119, 1549 );
        key( eraseBtn, 119, 1704 );

        // Pads 4 x 4 (numbers printed below each pad)
        pads.setBounds( 256, 897, 832, 845 );

        // Right of the pads: banks, mode keys, jog wheel
        mainControl.setBounds( 1140, 860, 650, 802 );

        // Q-link encoders P1-P4 right of the screen
        const int qy[] = { 150, 331, 507, 680 };
        for( int i = 0; i < 4; ++i )
            qlinks[(size_t)i]->setBounds( 1673 - 65, qy[i] - 55, 130, 147 );

        // Transport row
        const int tx[] = { 1200, 1327, 1453, 1580, 1709 };
        int       i    = 0;
        for( auto* b : { &recBtn, &overdubBtn, &stopBtn, &playBtn, &playStartBtn } )
            b->setBounds( tx[i++] - 60, 1694 - 44, 120, 125 );
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
