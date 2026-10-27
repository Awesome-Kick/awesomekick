#pragma once

#include <juce_events/juce_events.h>
#include <input_events/input_events.h>
#include <input_events/kick_SimulatorInterface.h>

namespace sim
{
    ///
    /// SimulatorInterface implementation registered with the Factory (type 0).
    /// Controls::InputEvents::initialize() (SIM_BUILD) creates the single instance.
    ///
    class SimulatorInterfaceImpl : public SimulatorInterface
    {
    public:
        SimulatorInterfaceImpl();
        ~SimulatorInterfaceImpl() override;

        /// App -> panel. Re-emitted asynchronously on the message thread through outputReceived.
        void sendMessageToHW( CPktMgr::HWMsg_t& f_HwMsg ) override;

        /// Panel -> app. Delivers a packet as if the front-panel MCU had sent it.
        static void injectInput( CPktMgr::HWMsg_t f_HwMsg );

        /// Packets the app sent to the "hardware" (LEDs, pad colours, slider LEDs...). Message thread only.
        inline static sigslot::signal<const CPktMgr::HWMsg_t&> outputReceived;

        static juce::String describe( const CPktMgr::HWMsg_t& f_HwMsg );

        /// Simulator trace: OutputDebugString, plus appended to the file named by KICKEM_LOG (if set).
        static void log( const juce::String& line );

    private:
        inline static SimulatorInterfaceImpl* instance = nullptr;
    };
}
