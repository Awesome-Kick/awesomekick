#include "kick_SimulatorMainControl.h"

namespace sim
{
    namespace
    {
        // Backlight colours of the keys (as lit on the hardware panel).
        const juce::Colour cyan { 0xff3fd4ff }, pink { 0xffff5fd2 }, magenta { 0xffe25cff }, blue { 0xff4f7dff }, teal { 0xff2fe0c0 },
            green { 0xff33e08a }, rose { 0xffff6f7d }, white { 0xfff2eef8 };
    }

    SimulatorMainControl::SimulatorMainControl()
    {
        std::vector<SimButton*> all;
        collectButtons( all );
        for( auto* b : all )
        {
            b->setStyle( SimButton::Style::key );
            addAndMakeVisible( b );
        }
        addAndMakeVisible( jog );
        jog.setKnobDiameter( 176.0f );

        // Navigation keys carry icons, no printed legend.
        backBtn.setStyle( SimButton::Style::key, SimButton::Icon::undo );
        copyBtn.setStyle( SimButton::Style::key, SimButton::Icon::copy );
        minusBtn.setStyle( SimButton::Style::key, SimButton::Icon::minus );
        plusBtn.setStyle( SimButton::Style::key, SimButton::Icon::plus );
        for( auto* b : { &backBtn, &copyBtn, &minusBtn, &plusBtn } )
            b->setCapSize( 72, 52 );
        shiftBtn.setCapSize( 98, 82 );

        // Backlight colours
        for( auto [b, c] : std::initializer_list<std::pair<SimButton*, juce::Colour>> {
                 { &bankA, cyan }, { &bankB, rose }, { &bankC, cyan }, { &bankD, rose },
                 { &mainBtn, cyan }, { &browseBtn, pink }, { &trackMixBtn, white }, { &trackMuteBtn, cyan }, { &nextSeqBtn, blue },
                 { &tapTempoBtn, pink }, { &noteRepeatBtn, blue }, { &menuBtn, teal }, { &shiftBtn, green },
                 { &stepSeqBtn, magenta }, { &tcBtn, pink }, { &samplerBtn, blue }, { &sampleEditBtn, magenta }, { &progEditBtn, teal },
                 { &backBtn, white }, { &copyBtn, white }, { &minusBtn, white }, { &plusBtn, white } } )
            b->setLedColour( c );

        // Jog wheel: rotation -> EN_KNOB_CENTER knob packets, centre push -> EN_KNOB_ENTER button packets.
        jog.onStep = []( int steps ) {
            hw::send( hw::knobMessage( DCC::EN_KNOB_CENTER, steps ) );
        };
        jog.onCentrePress = []( bool pressed ) {
            hw::send( hw::buttonMessage( DCC::EN_KNOB_ENTER, pressed ? DCC::BTN_PRESSED : DCC::BTN_RELEASED ) );
        };

        // SHIFT latches (click to engage, click again or use any button to release).
        shiftBtn.onPress = [this]( bool pressed ) {
            if( pressed )
                ShiftLatch::setLatched( !ShiftLatch::isLatched() );
        };
    }

    void SimulatorMainControl::collectButtons( std::vector<SimButton*>& out )
    {
        out.insert( out.end(), { &mainBtn, &browseBtn, &trackMixBtn, &trackMuteBtn, &nextSeqBtn, &stepSeqBtn, &tapTempoBtn, &noteRepeatBtn,
                                 &bankA, &bankB, &bankC, &bankD,
                                 &backBtn, &copyBtn, &minusBtn, &plusBtn,
                                 &tcBtn, &samplerBtn, &sampleEditBtn, &progEditBtn, &menuBtn, &shiftBtn } );
    }

    void SimulatorMainControl::setStudio( bool s )
    {
        studio = s;
        // Studio: jog group scaled by 1.2
        for( auto* b : { &backBtn, &copyBtn, &minusBtn, &plusBtn } )
            studio ? b->setCapSize( 86, 62 ) : b->setCapSize( 72, 52 );
        jog.setKnobDiameter( studio ? 211.0f : 176.0f );
        resized();
        repaint();
    }

    juce::Rectangle<int> SimulatorMainControl::navHousing() const
    {
        // Rounded housing around the jog wheel and its four keys
        if( studio )
            return juce::Rectangle<int>( 358, 382 ).withCentre( local( 1440, 1210 ) );
        auto tl = local( 1296, 1150 ); // board 1296..1594 x 1150..1468
        return { tl.x, tl.y, 298, 318 };
    }

    void SimulatorMainControl::paint( juce::Graphics& g )
    {
        // "BANK" heading with rules either side (panel only)
        if( !studio )
        {
            auto head = juce::Rectangle<int>( local( 1150, 862 ).x, local( 1150, 862 ).y, 610, 30 );
            g.setColour( palette::legend );
            g.setFont( juce::Font( 28.0f, juce::Font::bold ) );
            g.drawText( "BANK", head, juce::Justification::centred );
            auto ruleY = (float)head.getCentreY();
            g.drawLine( (float)head.getX() + 10.0f, ruleY, (float)head.getCentreX() - 46.0f, ruleY, 2.0f );
            g.drawLine( (float)head.getCentreX() + 46.0f, ruleY, (float)head.getRight() - 10.0f, ruleY, 2.0f );
        }

        // Jog housing
        const float k      = studio ? 1.2f : 1.0f;
        const float radius = 34.0f * k, seam = 30.0f * k;
        auto        h      = navHousing().toFloat();
        g.setColour( juce::Colour( 0xff111113 ) );
        g.fillRoundedRectangle( h, radius );
        g.setColour( juce::Colour( 0xff3a3a3f ) );
        g.drawRoundedRectangle( h, radius, 2.0f );
        // Seams between the four key segments
        g.setColour( juce::Colour( 0xff2a2a2e ) );
        g.drawLine( h.getCentreX(), h.getY(), h.getCentreX(), h.getY() + seam, 2.0f );
        g.drawLine( h.getCentreX(), h.getBottom() - seam, h.getCentreX(), h.getBottom(), 2.0f );
    }

    void SimulatorMainControl::resized()
    {
        studio ? layoutStudio() : layoutPanel();
    }

    void SimulatorMainControl::layoutPanel()
    {
        // All centres in board coordinates, measured from the hardware panel.
        auto key = [this]( SimButton& b, int cx, int cy, int w = 124, int h = 118 ) {
            auto p = local( cx, cy );
            b.setBounds( p.x - w / 2, p.y - 36, w, h );
        };

        // Banks
        key( bankA, 1197, 931 );
        key( bankB, 1365, 931 );
        key( bankC, 1534, 931 );
        key( bankD, 1704, 931 );

        // Row under the banks
        key( mainBtn, 1197, 1081 );
        key( browseBtn, 1324, 1081 );
        key( trackMixBtn, 1451, 1081 );
        key( trackMuteBtn, 1578, 1081 );
        key( nextSeqBtn, 1704, 1081 );

        // Left / right of the jog wheel
        key( tapTempoBtn, 1197, 1234 );
        key( noteRepeatBtn, 1197, 1389 );
        key( menuBtn, 1704, 1234 );
        {
            auto p = local( 1704, 1389 );
            shiftBtn.setBounds( p.x - 62, p.y - 42, 124, 100 );
        }

        // Bottom row
        key( stepSeqBtn, 1197, 1544 );
        key( tcBtn, 1324, 1544 );
        key( samplerBtn, 1451, 1544 );
        key( sampleEditBtn, 1578, 1544 );
        key( progEditBtn, 1704, 1544 );

        // Jog wheel and its keys (inside the housing corners)
        auto nav = [this]( SimButton& b, int cx, int cy ) {
            auto p = local( cx, cy );
            b.setBounds( p.x - 38, p.y - 27, 76, 56 );
        };
        nav( backBtn, 1338, 1188 );
        nav( copyBtn, 1552, 1188 );
        nav( minusBtn, 1338, 1430 );
        nav( plusBtn, 1552, 1430 );

        auto c = local( 1445, 1309 );
        jog.setBounds( juce::Rectangle<int>( 188, 188 ).withCentre( c ) );
    }

    void SimulatorMainControl::layoutStudio()
    {
        // Studio board 2560 x 1440 (see SimulatorBoard::resized), centres in board coordinates.
        auto key = [this]( SimButton& b, int cx, int cy ) {
            auto p = local( cx, cy );
            b.setBounds( p.x - 62, p.y - 36, 124, 118 );
        };

        // Top bar: the ten mode keys in one row
        int i = 0;
        for( auto* b : { &mainBtn, &browseBtn, &trackMixBtn, &trackMuteBtn, &nextSeqBtn, &stepSeqBtn, &tcBtn, &samplerBtn, &sampleEditBtn, &progEditBtn } )
            key( *b, 400 + 136 * i++, 63 );

        // Banks above the pad columns
        i = 0;
        for( auto* b : { &bankA, &bankB, &bankC, &bankD } )
            key( *b, 1766 + 224 * i++, 380 );

        // Second row under the display
        key( tapTempoBtn, 400, 1280 );
        key( noteRepeatBtn, 552, 1280 );
        key( menuBtn, 704, 1280 );
        {
            auto p = local( 856, 1280 );
            shiftBtn.setBounds( p.x - 62, p.y - 42, 124, 100 );
        }

        // Jog group (factor 1.2 of the panel's)
        auto nav = [this]( SimButton& b, int cx, int cy ) {
            auto p = local( cx, cy );
            b.setBounds( p.x - 46, p.y - 33, 92, 68 );
        };
        nav( backBtn, 1440 - 128, 1210 - 145 );
        nav( copyBtn, 1440 + 128, 1210 - 145 );
        nav( minusBtn, 1440 - 128, 1210 + 145 );
        nav( plusBtn, 1440 + 128, 1210 + 145 );
        jog.setBounds( juce::Rectangle<int>( 226, 226 ).withCentre( local( 1440, 1210 ) ) );
    }
}
