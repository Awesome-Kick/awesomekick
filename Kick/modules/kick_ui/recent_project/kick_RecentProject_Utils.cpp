#include "kick_ui/recent_project/kick_RecentProject_Utils.h"
namespace ui
{
    /////////////////////////////////////////////////////////////////
    ////////   LoadProjectAndAnimation   ////////////////////////////
    /////////////////////////////////////////////////////////////////

    LoadProjectAndAnimation::LoadProjectAndAnimation()
        : juce::Thread( "LoadProject" )
    {
        ae.addListener( this );
    }

    LoadProjectAndAnimation::~LoadProjectAndAnimation()
    {
        stopThread(-1);
        ae.removeListener( this );
    }

    void LoadProjectAndAnimation::startLoading( juce::File project )
    {
        if(isThreadRunning()) return;
        ui::RecentProject::isProjectLoading = true;
        proj                                = project;
        startThread();
        startAnimation();
    }

    void LoadProjectAndAnimation::onProjectLoaded( juce::File file )
    {
        juce::MessageManager::callAsync([this] {
            if(isAnimationRunning()) stopAnimation();
            kick_ui::signals::sigScreenOpen(kick_ui::Screen::HomeListView);
            ui::RecentProject::isProjectLoading = false;
        });
    }

    void LoadProjectAndAnimation::run()
    {
#if defined( SIM_BUILD ) && SIM_BUILD
        // Loading tears down and rebuilds tracks, programs and their listeners, which touch JUCE and UI
        // objects that are not thread-safe. Off the message thread this randomly crashed the simulator
        // ("pure virtual method called" on a freed message) for projects with notes. Load on the
        // message thread instead; this thread only waits, so isThreadRunning() still means "loading".
        auto done = std::make_shared<juce::WaitableEvent>();
        juce::MessageManager::callAsync( [this, done] {
            mt::getAudioInterface().loadProject( proj );
            done->signal();
        } );
        while( !done->wait( 100 ) )
            if( threadShouldExit() )
                return;
#else
        mt::getAudioInterface().loadProject( proj );
#endif
    }
}
