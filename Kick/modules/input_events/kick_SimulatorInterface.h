#pragma once

#include <3rdParty/signal/signal.hpp>
#include <PktMgr/PktMgr.hpp>

///
/// Bridge between Controls::InputEvents and a software control surface (AwesomeKick simulator, SIM_BUILD).
///
/// Both directions use the same CPktMgr::HWMsg_t packets that travel over the serial link
/// to/from the front-panel MCU, so the rest of the application cannot tell the difference.
///
class SimulatorInterface
{
public:
    virtual ~SimulatorInterface() = default;

    /// App -> control surface (LED / pad colour / system packets).
    virtual void sendMessageToHW( CPktMgr::HWMsg_t& f_HwMsg ) = 0;

    /// Control surface -> app. Emit with a packet laid out exactly as the MCU sends it.
    sigslot::signal<CPktMgr::HWMsg_t&> hwInputReceived;
};
