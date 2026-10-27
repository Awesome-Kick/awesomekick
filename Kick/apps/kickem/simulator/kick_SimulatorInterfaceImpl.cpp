#include <map>
#include <common/source/kick_Factory.h>
#include "kick_SimulatorInterfaceImpl.h"

namespace sim
{
    SimulatorInterfaceImpl::SimulatorInterfaceImpl()
    {
        instance = this;
    }

    SimulatorInterfaceImpl::~SimulatorInterfaceImpl()
    {
        if( instance == this )
            instance = nullptr;
    }

    juce::String SimulatorInterfaceImpl::describe( const CPktMgr::HWMsg_t& m )
    {
        juce::String s;
        s << "type=" << (int)m.pktType << " cmd=" << m.pktCmd << " dir=" << (int)m.pktDir << " data=[";
        for( size_t i = 0; i < m.data.size(); ++i )
            s << ( i ? " " : "" ) << m.data[i];
        s << "]";
        return s;
    }

    void SimulatorInterfaceImpl::log( const juce::String& line )
    {
        static const juce::String logPath = juce::SystemStats::getEnvironmentVariable( "KICKEM_LOG", {} );
        static juce::CriticalSection lock;

        const juce::String text = "[KickEM] " + line;
        juce::Logger::outputDebugString( text );

        if( logPath.isNotEmpty() )
        {
            const juce::ScopedLock sl( lock );
            juce::File( logPath ).appendText( juce::Time::getCurrentTime().formatted( "%H:%M:%S." )
                                                  + juce::String( juce::Time::getCurrentTime().getMilliseconds() ).paddedLeft( '0', 3 ) + " " + text + "\n" );
        }
    }

    void SimulatorInterfaceImpl::sendMessageToHW( CPktMgr::HWMsg_t& f_HwMsg )
    {
        static const bool traceOut = juce::SystemStats::getEnvironmentVariable( "KICKEM_TRACE_OUT", {} ).isNotEmpty();
        if( traceOut )
            log( "HW-OUT " + describe( f_HwMsg ) );

        CPktMgr::HWMsg_t copy = f_HwMsg;
        juce::MessageManager::callAsync( [copy]() { outputReceived( copy ); } );
    }

    void SimulatorInterfaceImpl::injectInput( CPktMgr::HWMsg_t f_HwMsg )
    {
        log( "HW-IN  " + describe( f_HwMsg ) );

        if( instance )
            instance->hwInputReceived( f_HwMsg );
        else
            log( "simulator interface not connected (InputEvents::initialize not called?)" );
    }

    REGISER_FACTORY( SimulatorInterface, SimulatorInterfaceImpl, 0 );
}
