#ifdef KICK_UI_H_INCLUDE
    /* When you add this cpp file to your project, you mustn't include it in a file where you've
already included any other headers - just put it inside a file on its own, possibly with your config
flags preceding it, but don't include anything else. That also includes avoiding any automatic prefix
header files that the compiler may be using.
*/
    #error "Incorrect use of JUCE cpp file"
#endif

#include "hardware/source/kick_Resorces.h"
#include "kick_ui_systemResources.h"

namespace kick_ui
{
    KSystemResources& KSystemResources::Instance()
    {
        static KSystemResources _sysR;
        return _sysR;
    }

    KSystemResources::KSystemResources()
    {
        setTitle( "SYSTEM RESOURCES" );

        mt::utils::addChildComponents( this, cpu, dsp, ram, flash, sdcard, usb );
        mt::utils::addAndMakeVisible( this, storage, purgeAll, purgeUnused, ok );

        cpu.setEnabled( false );
        cpu.setRange( 0.0, 100.0 );
        cpu.setTextValueSuffix( " %" );
        cpu.setTitleProportion( .35f );
        cpu.setValueProportion( .35f );
        cpu.showTitle( true, juce::Justification::centredTop );
        cpu.showValue( true, juce::Justification::centredBottom, juce::Slider::TextBoxRight );

        // Share of each audio block the engine spends processing: near 100 % means dropouts.
        dsp.setEnabled( false );
        dsp.setRange( 0.0, 100.0 );
        dsp.setTextValueSuffix( " %" );
        dsp.setTitleProportion( .35f );
        dsp.setValueProportion( .35f );
        dsp.showTitle( true, juce::Justification::centredTop );
        dsp.showValue( true, juce::Justification::centredBottom, juce::Slider::TextBoxRight );

        ram.setEnabled( false );
        ram.setRange( 0.0, 100.0 );
        ram.setTextValueSuffix( " %" );
        ram.setTitleProportion( .35f );
        ram.setValueProportion( .35f );
        ram.showTitle( true, juce::Justification::centred );
        ram.showValue( true, juce::Justification::centred, juce::Slider::TextBoxBelow );

        flash.setEnabled( false );
        flash.setRange( 0.0, 100.0 );
        flash.setTextValueSuffix( " %" );
        flash.setTitleProportion( .35f );
        flash.setValueProportion( .35f );
        flash.showTitle( true, juce::Justification::centred );
        flash.showValue( true, juce::Justification::centred, juce::Slider::TextBoxBelow );

        sdcard.setEnabled( false );
        sdcard.setRange( 0.0, 100.0 );
        sdcard.setTextValueSuffix( " %" );
        sdcard.setTitleProportion( .35f );
        sdcard.setValueProportion( .35f );
        sdcard.showTitle( true, juce::Justification::centred );
        sdcard.showValue( true, juce::Justification::centred, juce::Slider::TextBoxBelow );

        usb.setEnabled( false );
        usb.setRange( 0.0, 100.0 );
        usb.setTextValueSuffix( " %" );
        usb.setTitleProportion( .35f );
        usb.setValueProportion( .35f );
        usb.showTitle( true, juce::Justification::centred );
        usb.showValue( true, juce::Justification::centred, juce::Slider::TextBoxBelow );

        storage.setValueBackgroundColour( ui::Theme::backgroundColour( 9 ) );
        storage.setValueColour( ui::Theme::textColour( 2 ) );
        storage.setValueJustification( juce::Justification::centredTop );
        storage.setValueFontHeight( 16 );

#if !( JUCE_LINUX || __linux__ )
        // Desktop builds: the drive the app runs from stands in for the internal storage.
        internalPath = juce::File::getSpecialLocation( juce::File::currentApplicationFile ).getParentDirectory().getFullPathName().toStdString();
#endif

        purgeAll.setPlacementFlag( juce::Justification::centred );
        purgeUnused.setPlacementFlag( juce::Justification::centred );
        ok.setPlacementFlag( juce::Justification::centred );

        purgeAll.onClick    = [&]() {};
        purgeUnused.onClick = [&]() {};
        ok.onClick          = [&]() {
            mt::utils::exitDialog( this, UI::APPLY );
        };

        ui::themeChanged.connect( [&]() {
            purgeAll.setImages( "purge" );
            purgeUnused.setImages( "purgeused" );
            ok.setImages( "ok_resource" );
        } );
    }

    void KSystemResources::start()
    {
        startTimerHz( 1 );
    }

    void KSystemResources::stop()
    {
        stopTimer();
    }
    void KSystemResources::timerCallback()
    {
        double          cpuusage;
        mt::sysRamInfo  rInfo;
        mt::sysAppInfo  aInfo;
        mt::sysSizeInfo sInfo;
        auto&           res = mt::SystemResource::getResourceInterface();
        res.cpuInfo( cpuusage );
        res.ramInfo( rInfo );
        res.appInfo( aInfo );

        // Bars show the whole system; titles add this app's own share.
        auto setTitleText = []( ui::KickSlider& s, const juce::String& text ) {
            s.setName( text );
            s.setTitle( text );
            s.repaint();
        };
        setTitleText( cpu, "CPU  (KICK app " + juce::String( aInfo.cpu, 1 ) + " %)" );
        setTitleText( ram, "RAM  (KICK app " + juce::String( juce::roundToInt( aInfo.ramMB ) ) + " MB of " + juce::String( rInfo.totalRam / 1024.0, 1 ) + " GB)" );
        cpu.setValue( cpuusage, juce::dontSendNotification );
        ram.setValue( rInfo.perRam, juce::dontSendNotification );

        auto& dm = mt::getAudioInterface().getAudioDeviceManager();
        if( auto* dev = dm.getCurrentAudioDevice() )
            setTitleText( dsp, "AUDIO DSP  (" + juce::String( dev->getCurrentBufferSizeSamples() ) + " samples @ " + juce::String( juce::roundToInt( dev->getCurrentSampleRate() ) ) + " Hz)" );
        else
            setTitleText( dsp, "AUDIO DSP  (no audio device)" );
        dsp.setValue( juce::jlimit( 0.0, 100.0, dm.getCpuUsage() * 100.0 ), juce::dontSendNotification );

        res.sizeInfo( internalPath, sInfo );
        flash.setVisible( sInfo.status );
        flash.setValue( sInfo.perSize, juce::dontSendNotification );
        mt::SystemResource::getResourceInterface().sizeInfo( sdCardPath, sInfo );
        if( sInfo.status )
        {
            sdcard.setVisible( true );
            sdcard.setValue( sInfo.perSize, juce::dontSendNotification );
        }
        else
        {
            sdcard.setVisible( false );
        }
        mt::SystemResource::getResourceInterface().sizeInfo( usbDiskPath, sInfo );
        if( sInfo.status )
        {
            usb.setVisible( true );
            usb.setValue( sInfo.perSize, juce::dontSendNotification );
        }
        else
        {
            usb.setVisible( false );
        }
    }

    void KSystemResources::paint( juce::Graphics& g )
    {
        g.setColour( ui::Theme::backgroundColour( 14 ) );
        g.drawLine( 20.f, 196.f, 237.f, 196.f );
        g.drawLine( 367.f, 196.f, 584.f, 196.f );
    }

    void KSystemResources::resized()
    {
        cpu.setBounds( 90, 4, 425, 60 );
        dsp.setBounds( 90, 66, 425, 60 );
        ram.setBounds( 90, 128, 425, 60 );
        storage.setBounds( 267, 185, 71, 22 );

        flash.setBounds( 90, 208, 425, 70 );
        sdcard.setBounds( 90, 282, 425, 70 );

        usb.setBounds( 90, 357, 425, 70 );

        purgeAll.setBounds( 20, 433, 140, 40 );
        purgeUnused.setBounds( 240, 433, 140, 40 );
        ok.setBounds( 430, 433, 140, 40 );
    }

    void KSystemResources::visibilityChanged()
    {
        auto visible = isVisible();
        cpu.setVisible( visible );
        dsp.setVisible( visible );
        ram.setVisible( visible );
        storage.setVisible( visible );
        flash.setVisible( visible );
        sdcard.setVisible( visible );
        usb.setVisible( visible );
    }
}
