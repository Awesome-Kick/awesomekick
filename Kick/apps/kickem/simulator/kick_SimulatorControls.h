#pragma once

#include <functional>
#include <array>
#include <juce_gui_basics/juce_gui_basics.h>
#include <PktMgr/PktMgr.hpp>
#include <contracts/dcl/dcl_contracts.h>

///
/// Code-drawn front-panel controls for the AwesomeKick simulator.
/// Every interaction is turned into the CPktMgr::HWMsg_t the front-panel MCU would send
/// (layouts as read by contracts::DclContractsHelper) and injected via SimulatorInterfaceImpl.
///
namespace sim
{
    using DCC = contracts::DeviceControlContracts;

    namespace hw
    {
        /// EN_PKT_TYPE_BUTTON / EN_BTN_STATUS : { buttonCode (BtnLedSequence), BTN_PRESSED|BTN_RELEASED }
        CPktMgr::HWMsg_t buttonMessage( int buttonCode, int btnState );
        /// EN_PKT_TYPE_PAD / EN_PAD_STATE : { pad 0..15, BTN_PRESSED|BTN_RELEASED, velocity, aftertouch }
        CPktMgr::HWMsg_t padMessage( int pad, int btnState, int velocity, int aftertouch = 0 );
        /// EN_PKT_TYPE_KNOBS / EN_KNOB_ROTARY : { knob (KnobSequence), EN_KNOB_CLOCKWISE|EN_KNOB_ANTICLOCKWISE, |steps|, 0 }
        CPktMgr::HWMsg_t knobMessage( int knob, int steps );
        /// EN_PKT_TYPE_SLIDER / EN_SLIDER_TOUCH_INPUT : { value 0..100 }
        CPktMgr::HWMsg_t sliderMessage( int value );

        /// Deliver to the app through Controls::InputEvents (same path as the serial port).
        void send( const CPktMgr::HWMsg_t& msg );

        /// Pad colour id (contracts::DeviceControlContracts::PadColourIds) -> display colour.
        juce::Colour padColour( int colourId );
    }

    namespace palette
    {
        const juce::Colour body { 0xff353535 };
        const juce::Colour well { 0xff1b1b1b };
        const juce::Colour key { 0xff2a2a2a };
        const juce::Colour keyDown { 0xff4a4a4a };
        const juce::Colour outline { 0xff707070 };
        const juce::Colour text { 0xffe8e8e8 };
        const juce::Colour shiftText { 0xffffb608 };
        const juce::Colour led { 0xffff5c00 };

        // Front-panel look (hardware layout)
        const juce::Colour panel { 0xff262628 };
        const juce::Colour cap { 0xff17171a };
        const juce::Colour capDown { 0xff2c2c31 };
        const juce::Colour capEdge { 0xff050506 };
        const juce::Colour legend { 0xffc9c9cc };
        const juce::Colour unlit { 0xff8a8a90 };
        const juce::Colour ring { 0xffff2a2a };
    }

    ///
    /// Momentary hardware button with LED. Optional SHIFT function (alternate code).
    ///
    class SimButton : public juce::Component
    {
    public:
        SimButton( juce::String label, int code, juce::String shiftLabel = {}, int shiftCode = DCC::EN_INVALID_ID );

        int  getCode() const { return code; }
        int  getShiftCode() const { return shiftCode; }
        void setLed( bool on );
        void setLedColour( juce::Colour c ) { ledColour = c; repaint(); }
        bool isLedOn() const { return led; }

        /// Press / release exactly as a mouse click does (used by mouse handlers and the self-test).
        void press( bool isDown, bool shiftModifier = false );

        void paint( juce::Graphics& g ) override;
        void mouseDown( const juce::MouseEvent& e ) override;
        void mouseUp( const juce::MouseEvent& e ) override;

        /// Custom press handler (pressed=true/false). If unset, button messages are sent.
        std::function<void( bool pressed )> onPress;

        /// How the button is drawn.
        ///  panel: old simulator style (label + shift legend inside a large key)
        ///  key:   hardware key - small backlit cap at the top of the bounds, SHIFT legend printed below it
        ///  round: round transport key with an icon, name printed below
        enum class Style { panel, key, round };
        enum class Icon { none, rec, overdub, stop, play, playStart, undo, copy, minus, plus };
        void setStyle( Style s, Icon i = Icon::none ) { style = s; icon = i; repaint(); }
        /// Cap size in the key / round styles (default 98 x 70).
        void setCapSize( int w, int h ) { capW = w; capH = h; repaint(); }

    protected:
        juce::Rectangle<float> capArea() const;
        void drawIcon( juce::Graphics& g, juce::Rectangle<float> area, juce::Colour c ) const;

        juce::String label, shiftLabel;
        int          code, shiftCode;
        int          sentCode { DCC::EN_INVALID_ID };
        bool         down { false }, led { false };
        juce::Colour ledColour { palette::led };
        Style        style { Style::panel };
        Icon         icon { Icon::none };
        int          capW { 98 }, capH { 70 };
    };

    ///
    /// Latching SHIFT state shared by all buttons. Latch with the SHIFT button (or hold the PC Shift key
    /// while clicking). A latched SHIFT is released automatically after one shifted press.
    ///
    struct ShiftLatch
    {
        static bool isLatched();
        static void setLatched( bool latched ); // sends EN_SHIFT pressed / released
        static std::function<void( bool )> onChange;
    };

    ///
    /// Rotary control. Endless encoders emit step counts; absolute pots report 0..1.
    ///
    class SimKnob : public juce::Component
    {
    public:
        enum class Mode
        {
            endless, // Q-link / jog wheel: steps
            absolute // Master / Rec volume pots: value 0..1 (analog on the hardware)
        };

        SimKnob( juce::String label, Mode mode, bool angularDrag = false );

        std::function<void( int steps )>     onStep; // +cw / -ccw
        std::function<void( double value )>  onValue; // absolute mode
        std::function<void( bool pressed )>  onCentrePress; // optional push (jog wheel "enter")

        /// Turn by encoder detents (+cw / -ccw) / push the centre, exactly as the mouse does.
        void turn( int steps );
        void pushCentre( bool isDown );

        /// Hardware look: label above the knob (MASTER / REC) instead of below, and MIN / MAX marks.
        void setLabelAbove( bool above ) { labelAbove = above; repaint(); }
        void setShowMinMax( bool show ) { showMinMax = show; repaint(); }
        /// Fixed knob diameter (0 = fit the bounds).
        void setKnobDiameter( float d ) { knobDiameter = d; repaint(); }

        void paint( juce::Graphics& g ) override;
        void mouseDown( const juce::MouseEvent& e ) override;
        void mouseDrag( const juce::MouseEvent& e ) override;
        void mouseUp( const juce::MouseEvent& e ) override;
        void mouseWheelMove( const juce::MouseEvent& e, const juce::MouseWheelDetails& w ) override;

    private:
        juce::Rectangle<float> knobArea() const;
        bool                   inCentre( juce::Point<float> p ) const;
        void                   rotateBy( double degrees );

        juce::String label;
        Mode         mode;
        bool         angularDrag;
        double       angle { 0.0 }; // display angle (degrees)
        double       value { 0.75 }; // absolute mode
        double       accum { 0.0 };
        double       lastDragAngle { 0.0 };
        int          lastDragY { 0 };
        bool         centrePressed { false };
        double       wheelAccum { 0.0 };
        bool         labelAbove { false }, showMinMax { false };
        float        knobDiameter { 0.0f };
    };

    ///
    /// One drum pad. Colour/state are driven by the pad packets the app sends to the hardware.
    ///
    class SimPad : public juce::Component
    {
    public:
        explicit SimPad( int index );

        void setLit( bool lit, juce::Colour colour );
        /// Hit / release exactly as a mouse click does.
        void hit( bool isDown, int velocity = 100 );
        /// Pressure on a held pad, 0..127 (what dragging upwards does).
        void press( int pressure );
        void paint( juce::Graphics& g ) override;
        void mouseDown( const juce::MouseEvent& e ) override;
        void mouseDrag( const juce::MouseEvent& e ) override;
        void mouseUp( const juce::MouseEvent& e ) override;

    private:
        int          index;
        bool         lit { false }, down { false };
        int          lastVelocity { 0 }, lastPressure { 0 };
        juce::Colour colour { juce::Colours::grey };
    };

    ///
    /// 4x4 pad grid (pad 1 bottom-left, as on the hardware).
    ///
    class SimPadGrid : public juce::Component
    {
    public:
        SimPadGrid();

        void resized() override;
        void paint( juce::Graphics& g ) override;
        void setBankName( const juce::String& name );
        SimPad& getPad( int index ) { return *pads[(size_t)index]; }

        /// Apply an EN_PKT_TYPE_PAD packet sent by the app to the hardware.
        void applyPadPacket( const CPktMgr::HWMsg_t& m );

    private:
        void setPad( int pad, int state, int colourId );
        void refresh();

        std::array<std::unique_ptr<SimPad>, 16> pads;
        std::array<int, 16>                     padState {};
        std::array<int, 16>                     padColourId {};
        juce::String                            bankName { "A" };
    };

    ///
    /// Touch strip. Emits slider packets; shows the LED level the app sends back.
    ///
    class SimTouchSlider : public juce::Component
    {
    public:
        SimTouchSlider();
        void setLedLevel( int percent );
        void setTouch( int value ); // 0..100, sends a slider packet when it changes
        void paint( juce::Graphics& g ) override;
        void mouseDown( const juce::MouseEvent& e ) override;
        void mouseDrag( const juce::MouseEvent& e ) override;

    private:
        void touch( const juce::MouseEvent& e );
        int  touchValue { -1 };
        int  ledLevel { 0 };
    };
}
