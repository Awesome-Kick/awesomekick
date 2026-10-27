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
        s.push_back( [log] { log( "jog ENTER (load selected project)" ); } );
        s.push_back( [this] { mainControl.getJog().pushCentre( true ); } );
        s.push_back( [this] { mainControl.getJog().pushCentre( false ); } );
        wait( 12 );

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
