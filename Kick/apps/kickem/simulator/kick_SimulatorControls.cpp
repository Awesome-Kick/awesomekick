#include <kick_ui/kick_ui.h>
#include "kick_SimulatorControls.h"
#include "kick_SimulatorInterfaceImpl.h"

namespace sim
{
    //==============================================================================
    // Hardware packet builders
    //==============================================================================
    namespace hw
    {
        CPktMgr::HWMsg_t buttonMessage( int buttonCode, int btnState )
        {
            CPktMgr::HWMsg_t m;
            m.pktType = CPktMgr::EN_PKT_TYPE_BUTTON;
            m.pktCmd  = DCC::EN_BTN_STATUS;
            m.pktDir  = CPktMgr::EN_PKT_REQUEST;
            m.data    = { buttonCode, btnState };
            return m;
        }

        CPktMgr::HWMsg_t padMessage( int pad, int btnState, int velocity, int aftertouch )
        {
            CPktMgr::HWMsg_t m;
            m.pktType = CPktMgr::EN_PKT_TYPE_PAD;
            m.pktCmd  = DCC::EN_PAD_STATE;
            m.pktDir  = CPktMgr::EN_PKT_REQUEST;
            m.data    = { pad, btnState, velocity, aftertouch };
            return m;
        }

        CPktMgr::HWMsg_t knobMessage( int knob, int steps )
        {
            CPktMgr::HWMsg_t m;
            m.pktType = CPktMgr::EN_PKT_TYPE_KNOBS;
            m.pktCmd  = DCC::EN_KNOB_ROTARY;
            m.pktDir  = CPktMgr::EN_PKT_REQUEST;
            m.data    = { knob, steps >= 0 ? (int)DCC::EN_KNOB_CLOCKWISE : (int)DCC::EN_KNOB_ANTICLOCKWISE, std::abs( steps ), 0 };
            return m;
        }

        CPktMgr::HWMsg_t sliderMessage( int value )
        {
            CPktMgr::HWMsg_t m;
            m.pktType = CPktMgr::EN_PKT_TYPE_SLIDER;
            m.pktCmd  = DCC::EN_SLIDER_TOUCH_INPUT;
            m.pktDir  = CPktMgr::EN_PKT_REQUEST;
            m.data    = { value };
            return m;
        }

        void send( const CPktMgr::HWMsg_t& msg )
        {
            SimulatorInterfaceImpl::injectInput( msg );
        }

        juce::Colour padColour( int colourId )
        {
            if( colourId < 0 || colourId > DCC::PadColourNeutralGrey || colourId == DCC::PadColourTrans )
                return juce::Colours::transparentBlack;

            auto c = ui::Theme::padButtonColour( colourId );
            if( c.isTransparent() )
            {
                // Theme not loaded: fall back to a rough palette.
                static const juce::uint32 fallback[] = { 0xff8b0000, 0xff9f00c5, 0xffb5a300, 0xff90ee90, 0xff3cb371, 0xff2e8b57,
                                                         0xff87cefa, 0xff00ffff, 0xff1e90ff, 0xffff69b4, 0xffff1493, 0xffdb7093,
                                                         0xffff0000, 0xffffa500, 0xffff8c00, 0x00000000, 0xfff5f5f5, 0xff808080 };
                c = juce::Colour( fallback[colourId] );
            }
            return c.withAlpha( 1.0f );
        }
    }

    //==============================================================================
    // SimButton
    //==============================================================================
    SimButton::SimButton( juce::String l, int c, juce::String sl, int sc )
        : label( std::move( l ) ), shiftLabel( std::move( sl ) ), code( c ), shiftCode( sc )
    {
        setMouseClickGrabsKeyboardFocus( false );
        setWantsKeyboardFocus( false );
        setRepaintsOnMouseActivity( true );
    }

    void SimButton::setLed( bool on )
    {
        if( led != on )
        {
            led = on;
            repaint();
        }
    }

    juce::Rectangle<float> SimButton::capArea() const
    {
        auto b = getLocalBounds().toFloat();
        auto w = juce::jmin( (float)capW, b.getWidth() - 2.0f );
        auto h = juce::jmin( (float)capH, b.getHeight() - 2.0f );
        return juce::Rectangle<float>( w, h ).withCentre( { b.getCentreX(), b.getY() + 1.0f + h * 0.5f } );
    }

    void SimButton::drawIcon( juce::Graphics& g, juce::Rectangle<float> a, juce::Colour c ) const
    {
        g.setColour( c );
        const auto s  = juce::jmin( a.getWidth(), a.getHeight() );
        const auto ct = a.getCentre();
        auto box      = juce::Rectangle<float>( s, s ).withCentre( ct );
        juce::Path p;
        switch( icon )
        {
            case Icon::rec: g.fillEllipse( box.reduced( s * 0.18f ) ); break;
            case Icon::overdub:
                // Overdub: a figure with a recording dot - drawn as head + shoulders
                g.fillEllipse( juce::Rectangle<float>( s * 0.34f, s * 0.34f ).withCentre( { ct.x, ct.y - s * 0.14f } ) );
                g.fillRoundedRectangle( juce::Rectangle<float>( s * 0.52f, s * 0.26f ).withCentre( { ct.x, ct.y + s * 0.24f } ), s * 0.1f );
                break;
            case Icon::stop: g.fillRoundedRectangle( box.reduced( s * 0.24f ), s * 0.05f ); break;
            case Icon::play:
                p.addTriangle( ct.x - s * 0.2f, ct.y - s * 0.26f, ct.x - s * 0.2f, ct.y + s * 0.26f, ct.x + s * 0.26f, ct.y );
                g.fillPath( p );
                break;
            case Icon::playStart:
                p.addTriangle( ct.x - s * 0.3f, ct.y - s * 0.24f, ct.x - s * 0.3f, ct.y + s * 0.24f, ct.x + s * 0.08f, ct.y );
                g.fillPath( p );
                g.fillRect( juce::Rectangle<float>( s * 0.08f, s * 0.48f ).withCentre( { ct.x + s * 0.17f, ct.y } ) );
                g.fillRect( juce::Rectangle<float>( s * 0.08f, s * 0.48f ).withCentre( { ct.x + s * 0.3f, ct.y } ) );
                break;
            case Icon::undo:
            {
                p.addCentredArc( ct.x + s * 0.04f, ct.y + s * 0.06f, s * 0.24f, s * 0.24f, 0.0f, juce::degreesToRadians( -100.0f ), juce::degreesToRadians( 170.0f ), true );
                g.strokePath( p, juce::PathStrokeType( s * 0.07f ) );
                juce::Path head;
                auto tip = juce::Point<float>( ct.x - s * 0.2f, ct.y - s * 0.22f );
                head.addTriangle( tip.x - s * 0.12f, tip.y, tip.x + s * 0.1f, tip.y - s * 0.12f, tip.x + s * 0.1f, tip.y + s * 0.12f );
                g.fillPath( head );
                break;
            }
            case Icon::copy:
                g.drawRoundedRectangle( juce::Rectangle<float>( s * 0.36f, s * 0.44f ).withCentre( { ct.x - s * 0.07f, ct.y + s * 0.06f } ), s * 0.05f, s * 0.06f );
                g.drawRoundedRectangle( juce::Rectangle<float>( s * 0.36f, s * 0.44f ).withCentre( { ct.x + s * 0.09f, ct.y - s * 0.08f } ), s * 0.05f, s * 0.06f );
                break;
            case Icon::minus: g.fillRect( juce::Rectangle<float>( s * 0.42f, s * 0.08f ).withCentre( ct ) ); break;
            case Icon::plus:
                g.fillRect( juce::Rectangle<float>( s * 0.42f, s * 0.08f ).withCentre( ct ) );
                g.fillRect( juce::Rectangle<float>( s * 0.08f, s * 0.42f ).withCentre( ct ) );
                break;
            case Icon::none: break;
        }
    }

    void SimButton::paint( juce::Graphics& g )
    {
        if( style == Style::key || style == Style::round )
        {
            auto cap   = capArea();
            auto lit   = led ? ledColour : palette::unlit;
            auto hover = isMouseOver() && !down;

            // Drop shadow / recess
            g.setColour( palette::capEdge );
            if( style == Style::round )
                g.fillEllipse( cap.expanded( 3.0f ).translated( 0.0f, 2.0f ) );
            else
                g.fillRoundedRectangle( cap.expanded( 3.0f ).translated( 0.0f, 2.0f ), 9.0f );

            auto face = down ? cap.reduced( 1.0f ).translated( 0.0f, 1.0f ) : cap;
            g.setGradientFill( juce::ColourGradient( ( down ? palette::capDown : palette::cap ).brighter( hover ? 0.12f : 0.06f ), face.getX(), face.getY(),
                                                     down ? palette::capDown : palette::cap, face.getX(), face.getBottom(), false ) );
            if( style == Style::round )
                g.fillEllipse( face );
            else
                g.fillRoundedRectangle( face, 7.0f );

            g.setColour( led ? ledColour.withAlpha( 0.55f ) : juce::Colour( 0xff3a3a40 ) );
            if( style == Style::round )
                g.drawEllipse( face, led ? 2.0f : 1.0f );
            else
                g.drawRoundedRectangle( face, 7.0f, led ? 2.0f : 1.0f );

            if( icon != Icon::none )
            {
                auto iconArea = face.reduced( face.getHeight() * 0.22f );
                if( led )
                {
                    g.setColour( ledColour.withAlpha( 0.25f ) );
                    g.fillEllipse( iconArea.expanded( 6.0f ) );
                }
                drawIcon( g, iconArea, led ? ledColour.brighter( 0.3f ) : palette::unlit );
            }
            else
            {
                // Backlit legend on the cap
                g.setColour( led ? lit.brighter( 0.25f ) : lit );
                const auto fontH = juce::jmin( 24.0f, face.getHeight() * 0.38f );
                g.setFont( juce::Font( fontH, juce::Font::bold ) );
                const int lines = label.containsChar( ' ' ) && label.length() > 6 ? 2 : 1;
                g.drawFittedText( lines == 2 ? label.replaceFirstOccurrenceOf( " ", "\n" ) : label, face.reduced( 4.0f, 3.0f ).toNearestInt(),
                                  juce::Justification::centred, lines, 0.8f );
            }

            // Printed legend under the cap: SHIFT function (keys) or the name (round transport keys)
            auto below = getLocalBounds().toFloat().withTop( cap.getBottom() + 5.0f );
            const auto& text = style == Style::round ? label : shiftLabel;
            if( text.isNotEmpty() && below.getHeight() > 8.0f )
            {
                const bool shiftOn = style == Style::key && ShiftLatch::isLatched();
                g.setColour( shiftOn ? palette::shiftText : palette::legend );
                g.setFont( juce::Font( juce::jmin( 23.0f, below.getHeight() * 0.95f ), style == Style::round ? juce::Font::plain : juce::Font::bold ) );
                g.drawFittedText( text, below.toNearestInt(), juce::Justification::centredTop, 1, 0.75f );
            }
            return;
        }

        auto b = getLocalBounds().toFloat().reduced( 1.5f );
        g.setColour( down ? palette::keyDown : ( isMouseOver() ? palette::key.brighter( 0.08f ) : palette::key ) );
        g.fillRoundedRectangle( b, 5.0f );
        g.setColour( led ? ledColour : palette::outline );
        g.drawRoundedRectangle( b, 5.0f, led ? 2.5f : 1.0f );

        // LED dot
        auto dot = juce::Rectangle<float>( 7.0f, 7.0f ).withPosition( b.getRight() - 12.0f, b.getY() + 5.0f );
        g.setColour( led ? ledColour : juce::Colour( 0xff151515 ) );
        g.fillEllipse( dot );

        auto textArea = b.reduced( 4.0f, 2.0f );
        if( shiftLabel.isNotEmpty() )
        {
            auto top = textArea.removeFromTop( textArea.getHeight() * 0.58f );
            g.setColour( palette::text );
            g.setFont( juce::Font( juce::jmin( 19.0f, top.getHeight() * 0.75f ), juce::Font::bold ) );
            g.drawFittedText( label, top.toNearestInt(), juce::Justification::centredBottom, 1 );
            g.setColour( ShiftLatch::isLatched() ? palette::shiftText : palette::shiftText.withAlpha( 0.6f ) );
            g.setFont( juce::Font( juce::jmin( 15.0f, textArea.getHeight() * 0.9f ) ) );
            g.drawFittedText( shiftLabel, textArea.toNearestInt(), juce::Justification::centredTop, 1 );
        }
        else
        {
            g.setColour( palette::text );
            g.setFont( juce::Font( juce::jmin( 19.0f, textArea.getHeight() * 0.55f ), juce::Font::bold ) );
            g.drawFittedText( label, textArea.toNearestInt(), juce::Justification::centred, 2 );
        }
    }

    void SimButton::mouseDown( const juce::MouseEvent& e )
    {
        // Key styles: only the cap is clickable, not the printed legend under it.
        if( ( style == Style::key || style == Style::round ) && !capArea().expanded( 4.0f ).contains( e.position ) )
            return;
        press( true, e.mods.isShiftDown() );
    }
    void SimButton::mouseUp( const juce::MouseEvent& ) { press( false ); }

    void SimButton::press( bool isDown, bool shiftModifier )
    {
        if( isDown == down )
            return;
        down = isDown;
        repaint();

        if( onPress )
        {
            onPress( isDown );
            return;
        }

        if( isDown )
        {
            const bool shifted = ShiftLatch::isLatched() || shiftModifier;
            sentCode           = ( shifted && shiftCode != DCC::EN_INVALID_ID ) ? shiftCode : code;
            hw::send( hw::buttonMessage( sentCode, DCC::BTN_PRESSED ) );
            return;
        }

        if( sentCode != DCC::EN_INVALID_ID )
            hw::send( hw::buttonMessage( sentCode, DCC::BTN_RELEASED ) );
        sentCode = DCC::EN_INVALID_ID;

        // One-shot shift: release a latched SHIFT after it has been used.
        if( ShiftLatch::isLatched() )
            ShiftLatch::setLatched( false );
    }

    //==============================================================================
    // ShiftLatch
    //==============================================================================
    static bool                  shiftLatched = false;
    std::function<void( bool )> ShiftLatch::onChange;

    bool ShiftLatch::isLatched() { return shiftLatched; }

    void ShiftLatch::setLatched( bool latched )
    {
        if( shiftLatched == latched )
            return;
        shiftLatched = latched;
        hw::send( hw::buttonMessage( DCC::EN_SHIFT, latched ? DCC::BTN_PRESSED : DCC::BTN_RELEASED ) );
        if( onChange )
            onChange( latched );
    }

    //==============================================================================
    // SimKnob
    //==============================================================================
    static constexpr double degreesPerStep = 15.0; // one encoder detent
    static constexpr int    pixelsPerStep  = 6;

    SimKnob::SimKnob( juce::String l, Mode m, bool angular )
        : label( std::move( l ) ), mode( m ), angularDrag( angular )
    {
        setMouseClickGrabsKeyboardFocus( false );
        setWantsKeyboardFocus( false );
        if( mode == Mode::absolute )
            angle = -135.0 + value * 270.0;
    }

    juce::Rectangle<float> SimKnob::knobArea() const
    {
        auto b = getLocalBounds().toFloat();
        if( knobDiameter > 0.0f )
        {
            // Fixed geometry: label strip (30) above or below, MIN/MAX strip (40) below.
            auto top = b.getY() + ( labelAbove && label.isNotEmpty() ? 34.0f : 0.0f );
            return juce::Rectangle<float>( knobDiameter, knobDiameter ).withCentre( { b.getCentreX(), top + knobDiameter * 0.5f + 3.0f } );
        }
        auto text = label.isNotEmpty() ? juce::jmin( 20.0f, b.getHeight() * 0.18f ) : 0.0f;
        b.removeFromBottom( text );
        auto side = juce::jmin( b.getWidth(), b.getHeight() ) - 6.0f;
        return juce::Rectangle<float>( side, side ).withCentre( b.getCentre() );
    }

    bool SimKnob::inCentre( juce::Point<float> p ) const
    {
        auto k = knobArea();
        return onCentrePress && p.getDistanceFrom( k.getCentre() ) < k.getWidth() * 0.2f;
    }

    void SimKnob::paint( juce::Graphics& g )
    {
        auto k = knobArea();
        auto c = k.getCentre();
        auto r = k.getWidth() * 0.5f;

        if( knobDiameter > 0.0f )
        {
            // Hardware look: knurled black knob, white pointer; jog wheel with red LED ring.
            if( onCentrePress )
            {
                // LED ring
                for( int i = 3; i >= 1; --i )
                {
                    g.setColour( palette::ring.withAlpha( 0.12f * (float)i ) );
                    g.drawEllipse( k.reduced( 4.0f - (float)i * 2.0f ), 3.0f + (float)i * 2.5f );
                }
                g.setColour( palette::ring );
                g.drawEllipse( k.reduced( 4.0f ), 5.0f );
                r -= 12.0f;
            }

            // Shadow, body, knurling
            g.setColour( juce::Colour( 0xff050505 ) );
            g.fillEllipse( juce::Rectangle<float>( 2 * r + 4, 2 * r + 4 ).withCentre( c.translated( 2.0f, 4.0f ) ) );
            g.setGradientFill( juce::ColourGradient( juce::Colour( 0xff3d3d40 ), c.x - r, c.y - r, juce::Colour( 0xff0c0c0d ), c.x + r, c.y + r, false ) );
            g.fillEllipse( juce::Rectangle<float>( 2 * r, 2 * r ).withCentre( c ) );
            g.setColour( juce::Colour( 0xff000000 ).withAlpha( 0.55f ) );
            for( int i = 0; i < 36; ++i )
            {
                auto a = juce::MathConstants<float>::twoPi * (float)i / 36.0f;
                g.drawLine( { c.getPointOnCircumference( r * 0.9f, a ), c.getPointOnCircumference( r, a ) }, 1.4f );
            }
            auto top = r * 0.72f;
            g.setGradientFill( juce::ColourGradient( juce::Colour( 0xff4a4a4e ), c.x, c.y - top, juce::Colour( 0xff141416 ), c.x, c.y + top, false ) );
            g.fillEllipse( juce::Rectangle<float>( 2 * top, 2 * top ).withCentre( c ) );

            // Pointer
            auto a = juce::degreesToRadians( (float)angle );
            g.setColour( juce::Colours::white.withAlpha( 0.92f ) );
            g.drawLine( { c.getPointOnCircumference( onCentrePress ? r * 0.5f : r * 0.1f, a ), c.getPointOnCircumference( r * 0.88f, a ) },
                        juce::jmax( 3.0f, r * 0.07f ) );

            if( onCentrePress )
            {
                auto cap = juce::Rectangle<float>( r * 0.72f, r * 0.72f ).withCentre( c );
                g.setColour( centrePressed ? palette::capDown : juce::Colour( 0xff1c1c1f ) );
                g.fillEllipse( cap );
                g.setColour( juce::Colour( 0xff3a3a40 ) );
                g.drawEllipse( cap, 1.0f );
                g.setColour( palette::unlit.withAlpha( 0.7f ) );
                g.setFont( juce::Font( juce::jmin( 14.0f, r * 0.17f ), juce::Font::bold ) );
                g.drawText( "ENTER", cap, juce::Justification::centred );
            }

            g.setColour( palette::legend );
            if( label.isNotEmpty() )
            {
                g.setFont( juce::Font( 26.0f, juce::Font::bold ) );
                auto strip = labelAbove ? juce::Rectangle<float>( 0.0f, 0.0f, (float)getWidth(), 30.0f )
                                        : juce::Rectangle<float>( 0.0f, k.getBottom() + 6.0f, (float)getWidth(), 30.0f );
                g.drawText( label, strip, juce::Justification::centred );
            }
            if( showMinMax )
            {
                g.setFont( juce::Font( 19.0f, juce::Font::bold ) );
                auto y     = k.getBottom() + 4.0f;
                auto left  = c.getPointOnCircumference( r + 6.0f, juce::degreesToRadians( -135.0f ) );
                auto right = c.getPointOnCircumference( r + 6.0f, juce::degreesToRadians( 135.0f ) );
                g.fillEllipse( juce::Rectangle<float>( 5.0f, 5.0f ).withCentre( left ) );
                g.fillEllipse( juce::Rectangle<float>( 5.0f, 5.0f ).withCentre( right ) );
                g.drawText( "MIN", juce::Rectangle<float>( left.x - 40.0f, y + 9.0f, 80.0f, 24.0f ), juce::Justification::centred );
                g.drawText( "MAX", juce::Rectangle<float>( right.x - 40.0f, y + 9.0f, 80.0f, 24.0f ), juce::Justification::centred );
            }
            return;
        }

        if( mode == Mode::absolute )
        {
            juce::Path arc;
            arc.addCentredArc( c.x, c.y, r - 2.0f, r - 2.0f, 0.0f, juce::degreesToRadians( -135.0f ), juce::degreesToRadians( (float)angle ), true );
            g.setColour( palette::led );
            g.strokePath( arc, juce::PathStrokeType( 3.0f ) );
            r -= 6.0f;
        }

        g.setGradientFill( juce::ColourGradient( juce::Colour( 0xff5a5a5a ), c.x, c.y - r, juce::Colour( 0xff1e1e1e ), c.x, c.y + r, false ) );
        g.fillEllipse( juce::Rectangle<float>( 2 * r, 2 * r ).withCentre( c ) );
        g.setColour( juce::Colour( 0xff0e0e0e ) );
        g.drawEllipse( juce::Rectangle<float>( 2 * r, 2 * r ).withCentre( c ), 1.5f );

        // Indicator
        auto a  = juce::degreesToRadians( (float)angle );
        auto p1 = c.getPointOnCircumference( r * ( onCentrePress ? 0.55f : 0.25f ), a );
        auto p2 = c.getPointOnCircumference( r * 0.88f, a );
        g.setColour( juce::Colours::white );
        g.drawLine( { p1, p2 }, juce::jmax( 2.0f, r * 0.06f ) );

        if( onCentrePress )
        {
            auto cap = juce::Rectangle<float>( r * 0.8f, r * 0.8f ).withCentre( c );
            g.setColour( centrePressed ? palette::keyDown : palette::key );
            g.fillEllipse( cap );
            g.setColour( palette::outline );
            g.drawEllipse( cap, 1.0f );
            g.setColour( palette::text );
            g.setFont( juce::Font( juce::jmin( 13.0f, r * 0.2f ), juce::Font::bold ) );
            g.drawText( "ENTER", cap, juce::Justification::centred );
        }

        if( label.isNotEmpty() )
        {
            g.setColour( palette::text );
            g.setFont( juce::Font( juce::jmin( 18.0f, getHeight() * 0.17f ), juce::Font::bold ) );
            g.drawText( label, getLocalBounds().removeFromBottom( (int)juce::jmin( 25.0f, getHeight() * 0.21f ) ), juce::Justification::centred );
        }
    }

    void SimKnob::rotateBy( double degrees )
    {
        if( mode == Mode::absolute )
        {
            value = juce::jlimit( 0.0, 1.0, value + degrees / 270.0 );
            angle = -135.0 + value * 270.0;
            if( onValue ) onValue( value );
            repaint();
            return;
        }

        angle = std::fmod( angle + degrees, 360.0 );
        accum += degrees;
        int steps = (int)( accum / degreesPerStep );
        if( steps != 0 )
        {
            accum -= steps * degreesPerStep;
            if( onStep ) onStep( steps );
        }
        repaint();
    }

    void SimKnob::turn( int steps ) { rotateBy( steps * degreesPerStep ); }

    void SimKnob::pushCentre( bool isDown )
    {
        if( !onCentrePress || centrePressed == isDown )
            return;
        centrePressed = isDown;
        repaint();
        onCentrePress( isDown );
    }

    void SimKnob::mouseDown( const juce::MouseEvent& e )
    {
        if( inCentre( e.position ) )
        {
            pushCentre( true );
            return;
        }
        auto c        = knobArea().getCentre();
        lastDragAngle = std::atan2( e.position.y - c.y, e.position.x - c.x );
        lastDragY     = e.getPosition().y;
    }

    void SimKnob::mouseDrag( const juce::MouseEvent& e )
    {
        if( centrePressed )
            return;

        if( angularDrag )
        {
            auto   c     = knobArea().getCentre();
            double a     = std::atan2( e.position.y - c.y, e.position.x - c.x );
            double delta = a - lastDragAngle;
            if( delta > juce::MathConstants<double>::pi ) delta -= juce::MathConstants<double>::twoPi;
            if( delta < -juce::MathConstants<double>::pi ) delta += juce::MathConstants<double>::twoPi;
            lastDragAngle = a;
            rotateBy( juce::radiansToDegrees( delta ) );
        }
        else
        {
            int dy    = lastDragY - e.getPosition().y; // up = clockwise
            lastDragY = e.getPosition().y;
            rotateBy( dy * degreesPerStep / pixelsPerStep );
        }
    }

    void SimKnob::mouseUp( const juce::MouseEvent& )
    {
        pushCentre( false );
    }

    void SimKnob::mouseWheelMove( const juce::MouseEvent&, const juce::MouseWheelDetails& w )
    {
        const auto delta = w.isReversed ? -w.deltaY : w.deltaY;
        if( !w.isSmooth && delta != 0.0f )
        {
            // A mouse wheel notch is exactly one encoder step. Its deltaY differs per system
            // (~0.11 Windows, ~0.2 Linux), so scaling it gave 1 or 2 steps per notch.
            rotateBy( ( delta > 0.0f ? 1 : -1 ) * degreesPerStep );
            return;
        }
        wheelAccum += delta / 0.11; // trackpads: smooth scrolling, ~0.11 per step
        int notches = (int)wheelAccum;
        if( notches != 0 )
        {
            wheelAccum -= notches;
            rotateBy( notches * degreesPerStep );
        }
    }

    //==============================================================================
    // SimPad
    //==============================================================================
    SimPad::SimPad( int i )
        : index( i )
    {
        setMouseClickGrabsKeyboardFocus( false );
        setWantsKeyboardFocus( false );
    }

    void SimPad::setLit( bool l, juce::Colour c )
    {
        if( lit != l || colour != c )
        {
            lit    = l;
            colour = c;
            repaint();
        }
    }

    void SimPad::paint( juce::Graphics& g )
    {
        // Hardware look: black rubber pad, RGB light around its edge.
        auto b = getLocalBounds().toFloat().reduced( 3.0f );
        if( lit )
        {
            for( int i = 3; i >= 1; --i )
            {
                g.setColour( colour.withAlpha( 0.10f * (float)i ) );
                g.drawRoundedRectangle( b.expanded( (float)i * 1.5f ), 12.0f, 3.0f );
            }
        }
        g.setColour( juce::Colour( 0xff050506 ) );
        g.fillRoundedRectangle( b.translated( 0.0f, 3.0f ), 12.0f );

        auto face = down ? b.reduced( 2.0f ).translated( 0.0f, 2.0f ) : b;
        g.setGradientFill( juce::ColourGradient( juce::Colour( down ? 0xff26262a : 0xff1d1d20 ), face.getX(), face.getY(), juce::Colour( 0xff111113 ),
                                                 face.getRight(), face.getBottom(), false ) );
        g.fillRoundedRectangle( face, 12.0f );
        if( lit )
        {
            g.setColour( colour.withAlpha( down ? 0.30f : 0.10f ) );
            g.fillRoundedRectangle( face.reduced( 6.0f ), 8.0f );
        }
        g.setColour( lit ? colour : juce::Colour( 0xff2e2e33 ) );
        g.drawRoundedRectangle( face, 12.0f, lit ? 4.0f : 1.5f );

        g.setColour( palette::text.withAlpha( 0.8f ) );
        g.setFont( juce::Font( 15.0f, juce::Font::bold ) );
        if( down )
            g.drawText( "vel " + juce::String( lastVelocity ) + ( lastPressure ? "  prs " + juce::String( lastPressure ) : juce::String() ), b.reduced( 8.0f ),
                        juce::Justification::bottomRight );
    }

    void SimPad::mouseDown( const juce::MouseEvent& e )
    {
        // Velocity from click height: top = 127, bottom = 20.
        auto y = juce::jlimit( 0.0f, 1.0f, e.position.y / (float)juce::jmax( 1, getHeight() ) );
        hit( true, juce::jlimit( 1, 127, (int)std::round( 127.0f - y * 107.0f ) ) );
    }

    void SimPad::mouseUp( const juce::MouseEvent& ) { hit( false ); }

    void SimPad::mouseDrag( const juce::MouseEvent& e )
    {
        // Dragging up while held presses harder: a pad's height = full pressure.
        const auto up = (float)-e.getDistanceFromDragStartY() / (float)juce::jmax( 1, getHeight() );
        press( juce::jlimit( 0, 127, (int)std::round( up * 127.0f ) ) );
    }

    void SimPad::press( int pressure )
    {
        if( !down || pressure == lastPressure )
            return;
        lastPressure = pressure;
        repaint();
        // The panel's pad packet with the pressure byte set; neither a press nor a release.
        hw::send( hw::padMessage( index, DCC::BTN_LONGPRESSED, lastVelocity, pressure ) );
    }

    void SimPad::hit( bool isDown, int velocity )
    {
        if( down == isDown )
            return;
        down = isDown;
        if( isDown )
            lastVelocity = velocity;
        lastPressure = 0;
        repaint();
        hw::send( hw::padMessage( index, isDown ? DCC::BTN_PRESSED : DCC::BTN_RELEASED, isDown ? velocity : 0 ) );
    }

    //==============================================================================
    // SimPadGrid
    //==============================================================================
    SimPadGrid::SimPadGrid()
    {
        padColourId.fill( DCC::PadColourTrans );
        padState.fill( DCC::StateOff );
        for( int i = 0; i < 16; ++i )
        {
            pads[i] = std::make_unique<SimPad>( i );
            addAndMakeVisible( *pads[i] );
        }
    }

    void SimPadGrid::setBankName( const juce::String& name )
    {
        bankName = name;
        repaint();
    }

    void SimPadGrid::paint( juce::Graphics& g )
    {
        // Pad numbers printed under the bottom-left corner of each pad, as on the hardware.
        g.setColour( palette::legend );
        g.setFont( juce::Font( 23.0f, juce::Font::bold ) );
        for( int i = 0; i < 16; ++i )
        {
            auto pb = pads[(size_t)i]->getBounds();
            g.drawText( juce::String( i + 1 ), juce::Rectangle<int>( pb.getX() + 8, pb.getBottom() + 3, 60, 28 ), juce::Justification::centredLeft );
        }
    }

    void SimPadGrid::resized()
    {
        // Hardware proportions: pad 181 x 176 on a 217 x 213 pitch (measured from the panel), numbers below.
        const float padRatioX = 181.0f / 217.0f, padRatioY = 176.0f / 213.0f;
        const float labelH    = 30.0f;
        const float pitchX    = (float)getWidth() / ( 3.0f + padRatioX );
        const float pitchY    = ( (float)getHeight() - labelH ) / ( 3.0f + padRatioY );
        const int   w         = juce::roundToInt( pitchX * padRatioX );
        const int   h         = juce::roundToInt( pitchY * padRatioY );
        for( int i = 0; i < 16; ++i )
        {
            int row = 3 - i / 4; // pad 1 bottom-left
            int col = i % 4;
            pads[(size_t)i]->setBounds( juce::roundToInt( col * pitchX ), juce::roundToInt( row * pitchY ), w, h );
        }
    }

    void SimPadGrid::setPad( int pad, int state, int colourId )
    {
        if( pad < 0 || pad >= 16 )
            return;
        padState[pad]    = state;
        padColourId[pad] = colourId;
    }

    void SimPadGrid::refresh()
    {
        for( int i = 0; i < 16; ++i )
        {
            auto c   = hw::padColour( padColourId[i] );
            bool lit = padState[i] != DCC::StateOff && !c.isTransparent();
            pads[i]->setLit( lit, c );
        }
    }

    void SimPadGrid::applyPadPacket( const CPktMgr::HWMsg_t& m )
    {
        const auto& d = m.data;
        auto        n = (int)d.size();
        switch( m.pktCmd )
        {
            case DCC::EN_PAD_SET_SINGLE_PAD: // { pad, state, colour }
            case DCC::EN_PAD_SET_STEP_SINGLE_PAD: // { pad, state, colour, velocity }
                if( n >= 3 ) setPad( d[0], d[1], d[2] );
                break;

            case DCC::EN_PAD_SET_BLINKING: // { pad, state, onColour, offColour }
                if( n >= 3 ) setPad( d[0], d[1], d[2] );
                break;

            case DCC::EN_PAD_SET_ALL_SINGLE_COLOUR: // { state, colour }
                if( n >= 2 )
                    for( int i = 0; i < 16; ++i ) setPad( i, d[0], d[1] );
                break;

            case DCC::EN_PAD_SET_ALL_MULTI_COLOUR: // { state, colour x16 }
                for( int i = 0; i < 16 && i + 1 < n; ++i ) setPad( i, d[0], d[i + 1] );
                break;

            case DCC::EN_PAD_SET_COLOUR_STATE: // { colour x N, state x N }
            {
                int count = n / 2;
                for( int i = 0; i < count && i < 16; ++i ) setPad( i, d[count + i], d[i] );
                break;
            }

            case DCC::EN_PAD_SET_STEP_ALL_PAD: // { state, colour x16, velocity x16 }
                for( int i = 0; i < 16 && i + 1 < n; ++i ) setPad( i, d[0], d[i + 1] );
                break;

            default: return; // settings / colour mode / aftertouch: nothing to show
        }
        refresh();
    }

    //==============================================================================
    // SimTouchSlider
    //==============================================================================
    SimTouchSlider::SimTouchSlider()
    {
        setMouseClickGrabsKeyboardFocus( false );
        setWantsKeyboardFocus( false );
    }

    void SimTouchSlider::setLedLevel( int percent )
    {
        ledLevel = juce::jlimit( 0, 100, percent );
        repaint();
    }

    void SimTouchSlider::paint( juce::Graphics& g )
    {
        // Hardware look: recessed touch strip, a column of small LED dashes on its right side.
        auto b = getLocalBounds().toFloat();
        g.setColour( juce::Colour( 0xff0b0b0c ) );
        g.fillRoundedRectangle( b, 6.0f );
        auto inner = b.reduced( 12.0f );
        g.setGradientFill( juce::ColourGradient( juce::Colour( 0xff1e1e21 ), inner.getX(), inner.getY(), juce::Colour( 0xff141416 ), inner.getRight(),
                                                 inner.getBottom(), false ) );
        g.fillRect( inner );
        g.setColour( juce::Colour( 0xff2f2f34 ) );
        g.drawRect( inner, 1.5f );

        const int segments = 20;
        auto      column   = inner.withLeft( inner.getRight() - 46.0f ).reduced( 14.0f, 10.0f );
        auto      segH     = column.getHeight() / segments;
        for( int i = 0; i < segments; ++i )
        {
            auto seg = juce::Rectangle<float>( column.getX(), column.getBottom() - ( i + 1 ) * segH + segH * 0.25f, column.getWidth(), segH * 0.5f );
            bool on  = ledLevel > 0 && ( i + 1 ) * 100 / segments <= ledLevel;
            if( on )
            {
                g.setColour( palette::ring.withAlpha( 0.3f ) );
                g.fillRoundedRectangle( seg.expanded( 4.0f ), 4.0f );
            }
            g.setColour( on ? palette::ring : juce::Colour( 0xff050505 ) );
            g.fillRect( seg );
        }

        if( touchValue >= 0 )
        {
            auto y = inner.getBottom() - inner.getHeight() * touchValue / 100.0f;
            g.setColour( juce::Colours::white.withAlpha( 0.6f ) );
            g.drawLine( inner.getX() + 6.0f, y, column.getX() - 8.0f, y, 2.0f );
        }
    }

    void SimTouchSlider::touch( const juce::MouseEvent& e )
    {
        auto top    = 12.0f;
        auto height = juce::jmax( 1.0f, (float)getHeight() - 24.0f );
        setTouch( (int)std::round( 100.0f * ( 1.0f - ( e.position.y - top ) / height ) ) );
    }

    void SimTouchSlider::setTouch( int value )
    {
        int v = juce::jlimit( 0, 100, value );
        if( v != touchValue )
        {
            touchValue = v;
            repaint();
            hw::send( hw::sliderMessage( v ) );
        }
    }

    void SimTouchSlider::mouseDown( const juce::MouseEvent& e ) { touch( e ); }
    void SimTouchSlider::mouseDrag( const juce::MouseEvent& e ) { touch( e ); }
}
