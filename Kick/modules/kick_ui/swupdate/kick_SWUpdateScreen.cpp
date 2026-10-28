#include "kick_SWUpdateScreen.h"
namespace kick_ui
{

    // Firmware-update shell commands (mkdir/mount/tail/reboot). The simulator build never runs them
    // on the host computer; on Linux the link-time sandbox (kick_SimSandbox.cpp) would catch them too,
    // this also covers macOS and Windows.
    static int swuShell( const char* cmd )
    {
#if defined( SIM_BUILD ) && SIM_BUILD
        std::fprintf( stderr, "[Kickray sandbox] blocked system(): %s\n", cmd );
        return -1;
#else
        return system( cmd );
#endif
    }

    SWUpdateScreen::SWUpdateScreen()
        : kick_ui::IScreen( kick_ui::Screen::SWUpdateWindow )
    {
        mt::utils::addAndMakeVisible( this, updateLabel, restartLabel, poweroffLabel, stepProgressbar, overallProgressbar, backBtn, logo, stepLabel, overallLabel, header, logo1 );

        updateLabel.setValueJustification( juce::Justification::centred );
        restartLabel.setValueJustification( juce::Justification::centred );
        poweroffLabel.setValueJustification( juce::Justification::centred );
        header.showOptions( false, false );
        header.showBackButton( false );
        header.onBackButtonPressed = [this]( bool ) { closeScreen(); };
        updateLabel.setValueColour( ui::Theme::textColour( 2 ) );
        restartLabel.setValueColour( ui::Theme::textColour( 1 ) );
        poweroffLabel.setValueColour( ui::Theme::textColour( 2 ) );
        mt::utils::setValueFontHeight( 20, updateLabel, poweroffLabel );
        updateLabel.setValueBackgroundColour( ui::Theme::backgroundColour( 9 ) );
        restartLabel.setValueBackgroundColour( ui::Theme::backgroundColour( 9 ) );
        poweroffLabel.setValueBackgroundColour( ui::Theme::backgroundColour( 9 ) );

        updateLabel.setFontHeight( 20 );
        restartLabel.setFontHeight( 18 );
        poweroffLabel.setFontHeight( 20 );

        stepLabel.setFontHeight( 18 );
        overallLabel.setFontHeight( 18 );

        stepLabel.setValueJustification( juce::Justification::centredTop );
        overallLabel.setValueJustification( juce::Justification::centredTop );

        stepLabel.setValueBackgroundColour( ui::Theme::backgroundColour( 9 ) );
        overallLabel.setValueBackgroundColour( ui::Theme::backgroundColour( 9 ) );

        stepLabel.setValueColour( ui::Theme::textColour( 1 ) );
        overallLabel.setValueColour( ui::Theme::textColour( 1 ) );

        backBtn.onClick = [&]() {
            closeScreen();
        };
        stepProgressbar.setVisible( false );
        stepLabel.setVisible( false );
        setSwUpdateCallback();
        setUsbCallback();
        startTimerHz( 10 );
    }

    void SWUpdateScreen::paint( juce::Graphics& g )
    {
        // g.setColour( ui::Theme::backgroundColour( 1 ) );
        // g.fillAll();
        mt::utils::fillRoundedRect( g, centreBounds, ui::Theme::backgroundColour( 1 ), 6.0f, false, false, true, true );
    }

    void SWUpdateScreen::visibilityChanged()
    {
        if( !isVisible() || updateStarted ) return;
        updateStarted = true;
        downloadCompleted = false;
        setUsbCallback();
        setSwUpdateCallback();
        _overallLabel      = "";
        _overallpercentage = 0.0;
        if( isVisible() )
        {
            DBG( "SWUPDATE SCREEN IS VISIBILE" );
            if( mt::FirmwareUpdate::getSwupdateInstance().getSwUpdateMode() == mt::SwUpdateMode_t::PC )
            {
                stepProgressbar.setVisible( false );
                _overallLabel = "Waiting for connection";
                // mt::USBFunctionFS::getUSBInstance().start();
                DBG( "its in PC Mode" );
            }
            else
            {
                std::thread( []() {
                    mt::FirmwareUpdate::getSwupdateInstance().updateFirmware();
                } ).detach();
            }
        }
    }

    void SWUpdateScreen::resized()
    {
        auto b = getLocalBounds();
        header.setBounds( b.removeFromTop( ui::layout::header.getHeight() ) );
        logo1.setBounds( 410, 100, 186, 30 );
        logo.setBounds( b.removeFromTop( 153 ).withTrimmedTop( 100 ) );
        updateLabel.setBounds( b.removeFromTop( 80 ).withTrimmedTop( 25 ) );
        poweroffLabel.setBounds( b.removeFromTop( 25 ).withTrimmedTop( 5 ) );
        restartLabel.setBounds( b.removeFromTop( 50 ).withTrimmedTop( 10 ) );

        stepProgressbar.setBounds( 300, 320, 425, 40 );
        stepLabel.setBounds( b.removeFromTop( 20 ) );
        overallProgressbar.setBounds( b.removeFromTop( 130 ).withTrimmedTop( 20 ) );
        overallLabel.setBounds( b.removeFromTop( 20 ) );
        //  backBtn.setBounds( 10, 556, 120, 44 );
    }

    void SWUpdateScreen::timerCallback()
    {
        overallProgressbar.updateProgress( _overallpercentage );
        overallLabel.setText( _overallLabel, juce::dontSendNotification );
    }

    void SWUpdateScreen::getSwupdateStatus( mt::swupdatemsg_t& data )
    {
        if( !juce::MessageManager::getInstance()->isThisTheMessageThread() )
        {
            juce::Component::SafePointer<SWUpdateScreen> safe(this);
            juce::MessageManager::callAsync([safe, data]() mutable { if( safe ) safe->getSwupdateStatus(data); });
            return;
        }
        if( data.command_status == mt::RECOVERY_STATUS::RUN )
        {
            _overallpercentage = data.totalper;
            _step_percentage   = data.cur_percent;
            _overallLabel      = juce::String( data.curstep ) + "/" + juce::String( data.nsteps );
        }
        else if( data.command_status == mt::RECOVERY_STATUS::LINUX_SUCCESS )
        {
            _overallpercentage = data.totalper;
            _step_percentage   = data.cur_percent;
            _overallLabel      = juce::String( data.curstep ) + "/" + juce::String( data.nsteps );
        }
        else if( data.command_status == mt::RECOVERY_STATUS::SUCCESS )
        {
            updateStarted = false;
            _overallpercentage = 100.0;
            DBG( "Firmware Upgrade SUCCESS " );
            auto warningPopup = new ui::FilePopup( "Firmware Upgrade SUCCESS! Do you want to reboot a device ?", false, true, true );
            mt::utils::showDialog( "SUCCESS!", warningPopup, [this, warningPopup]( int R ) {
                if( R == UI::APPLY )
                {
                    if( swuShell( "sync && reboot" ) == 0 )
                        DBG( "Rebooting the device so new firmware will apply" );
                }
                else if( R == UI::CANCEL )
                    closeScreen();
                if( warningPopup ) delete warningPopup;
            } );
        }
        else if( data.command_status == mt::RECOVERY_STATUS::FAILURE || data.command_status == mt::RECOVERY_STATUS::LINUX_FAILURE )
        {
            updateStarted = false;
            auto warningPopup = new ui::FilePopup( "Firmware Update Failed", true, false, false );
            mt::utils::showDialog( "WARNING!", warningPopup, [this, warningPopup]( int R ) {
                if( R == UI::CANCEL )
                    closeScreen();
                if( warningPopup ) delete warningPopup;
            } );
            mt::USBFunctionFS::getUSBInstance().stop();
            DBG( "Firmware Upgrade FAILED" );
            header.showBackButton( true );
        }
    }

    void SWUpdateScreen::getUsbupdateStatus( mt::usbmsg_t& data )
    {
        if( !juce::MessageManager::getInstance()->isThisTheMessageThread() )
        {
            juce::Component::SafePointer<SWUpdateScreen> safe(this);
            juce::MessageManager::callAsync([safe, data]() mutable { if( safe ) safe->getUsbupdateStatus(data); });
            return;
        }
        if(downloadCompleted) return; // Ignore late USB status after ownership passes to the flasher.
        if( data.command_status == mt::USB_STATUS::U_INIT )
        {
            DBG( "INIT" );
            _overallLabel      = "Waiting for connection";
            _overallpercentage = 0.0;
            DBG( "Firmware is Getting Download Done " << data.totalfilesize );
        }
        else if( data.command_status == mt::USB_STATUS::U_DOWNLOAD )
        {
            DBG( "DOWNLOAD" );
            DBG( "Firmware is Getting Download Done " << data.downloadfilesize );
            DBG( "percentage " << data.percentage );
            _overallpercentage = data.percentage;
            _overallLabel      = "Downloading ..";
        }
        else if( data.command_status == mt::USB_STATUS::U_DONE )
        {
            downloadCompleted = true;
            _overallLabel = "Download done..";
            DBG( "USB Download DONE<<" << juce::String( data.percentage ) );
            _overallLabel      = "Validating Image....";
            _overallpercentage = 0;
            mt::USBFunctionFS::getUSBInstance().stop();
            juce::Component::SafePointer<SWUpdateScreen> safe(this);
            std::thread([safe] {
                auto& updater = mt::FirmwareUpdate::getSwupdateInstance();
                const auto status = updater.validateImage(true);
                if(status == mt::SwUpdateErrorCode_t::SW_SUCCESS) updater.updateFirmware();
                else juce::MessageManager::callAsync([safe] {
                    if(!safe) return;
                    safe->updateStarted = false;
                    safe->_overallLabel = "Image validation failed";
                    safe->header.showBackButton(true);
                });
            }).detach();
        }
        else if( data.command_status == mt::USB_STATUS::U_FAILED )
        {
            updateStarted = false;
            header.showBackButton(true);
            DBG( "Firmware Download FAILED" );
            _overallLabel      = "Download done..";
            _overallpercentage = 0.0;
            mt::USBFunctionFS::getUSBInstance().stop();
        }
        else if( data.command_status == mt::USB_STATUS::U_TIMEOUT )
        {
            updateStarted = false;
            DBG( "U_TIMEOUT" );
            _overallLabel      = "Connection Timed Out";
            _overallpercentage = 0.0;
            mt::USBFunctionFS::getUSBInstance().stop();
            closeScreen();
            //add popup with timeout msg;
        }
        else
        {
            DBG( "UNKONWN COMMAND" );
        }
    }

    void SWUpdateScreen::handleCommand( CPktMgr::HWMsg_t& f_hwMsg )
    {
        mt::MCUUpdateAction flag = mt::MCUUpdateAction::EN_SW_UPDATE_FAILURE;
        if( contracts::DclContractsHelper::isSoftwareUpdateMessage( f_hwMsg ) && !f_hwMsg.data.empty() )
        {
#ifdef DEBUG_FWUPDATE
            DBG( "SwUtility - Handle Command Enter" );
#endif

            if( f_hwMsg.data[0] == 1 )
            {
                flag = mt::MCUUpdateAction::EN_SW_UPDATE_BEGIN_TRANSFER;
#ifdef DEBUG_FWUPDATE
                DBG( "Begin Transfer" );
#endif
            }
            else if( f_hwMsg.data[0] == 6 )
            {
                flag = mt::MCUUpdateAction::EN_SW_UPDATE_FAILURE;
            }
            else
            {
                flag = mt::MCUUpdateAction::EN_SW_UPDATE_ACK;
#ifdef DEBUG_FWUPDATE
                DBG( "Send Ack" );
#endif
            }
            mt::FirmwareUpdate::getSwupdateInstance().setMcuUpdateState( flag );
        }
    }
}
