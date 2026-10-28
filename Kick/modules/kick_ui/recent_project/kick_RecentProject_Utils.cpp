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
        mt::getAudioInterface().loadProject( proj );
    }
}
