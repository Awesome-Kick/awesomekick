#pragma once
#include <hardware/source/kick_Hwcommon.h>
#include <hardware/source/kick_FirmwareUpdate.h>
#include <hardware/source/kick_UsbFunctionFS.h>

namespace kick_ui
{
    class SWUpdateScreen : public kick_ui::IScreen
        , public juce::Component
        , private juce::Timer
    {
    public:
        SWUpdateScreen();
        ~SWUpdateScreen() {}

        void getSwupdateStatus( mt::swupdatemsg_t& swupdate );
        void getUsbupdateStatus( mt::usbmsg_t& usbupdate );
        void setSwUpdateCallback()
        {
            juce::Component::SafePointer<SWUpdateScreen> safe(this);
            mt::FirmwareUpdate::getSwupdateInstance().setSwUpdatecallback([safe](mt::swupdatemsg_t& data) {
                juce::MessageManager::callAsync([safe, data]() mutable { if(safe) safe->getSwupdateStatus(data); });
            });
        }
        void setUsbCallback()
        {
            juce::Component::SafePointer<SWUpdateScreen> safe(this);
            mt::USBFunctionFS::getUSBInstance().setUsbcallback([safe](mt::usbmsg_t& data) {
                juce::MessageManager::callAsync([safe, data]() mutable { if(safe) safe->getUsbupdateStatus(data); });
            });
        }

    private:
        bool requiresControlBar() override { return false; };
        bool requiresHandleCommand() override { return true; };

        void                 timerCallback();
        bool                 requiresStatusBar() override { return false; };
        void                 handleCommand( CPktMgr::HWMsg_t& ) override;
        void                 paint( juce::Graphics& g ) override;
        juce::Rectangle<int> centreBounds = { 10, 90, 1002, 500 };
        void                 resized() override;
        void                 visibilityChanged() override;

        ui::KickLabel           updateLabel{ "UPDATING FIRMWARE" };
        ui::KickLabel           poweroffLabel{ "Do not power off the device." };
        ui::KickLabel           restartLabel{ "Kick will automatically restart after updating." };
        kick_ui::KickModeHeader header{ "Firmware Update", "Icon_SettingsScreen" };
        ui::KickProgressBar     stepProgressbar;
        ui::KickProgressBar     overallProgressbar;
        bool updateStarted{false};
        bool downloadCompleted{false};
        double                  _step_percentage{0};
        double                  _overallpercentage{0};
        juce::String            _overallLabel = "Downloading";

        ui::KickImageButton logo1{ "", "logo_pref", "" };
        ui::KickImageButton logo{ "", "_Layer_.png", "" };
        ui::KickLabel       stepLabel{ "Step 1/4" };
        ui::KickLabel       overallLabel{ "Download done.." };

        //temporary
        ui::KickButton backBtn{ "Go Back" };
    };
}
