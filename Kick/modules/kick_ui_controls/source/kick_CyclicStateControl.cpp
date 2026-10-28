namespace ui
{

    KickCyclicStateControl::KickCyclicStateControl()
    {
        mt::utils::addAndMakeVisible( this, leftNav, rightNav );
        mt::utils::addListener( this, leftNav, rightNav );
        addAndMakeVisible( stateButton );

        ui::connectTheme( *this, [&]() {
            if( currentState < states.size() ) stateButton.setImages( states[currentState] );
        } );
    }

    void KickCyclicStateControl::resized()
    {
        int buttonWidth = getWidth() - ( 2 * 40 ) - ( 2 * 2 );
        leftNav.setBounds( 0, 0, 40, getHeight() );
        stateButton.setBounds( leftNav.getRight() + 2, 0, buttonWidth, getHeight() );
        rightNav.setBounds( stateButton.getRight() + 2, 0, 40, getHeight() );
    }

    void KickCyclicStateControl::setCurrentState( unsigned int cs )
    {
        if( states.empty() ) return;
        currentState = juce::jmin( cs, static_cast<unsigned>(states.size() - 1) );
        stateButton.setImages( states[currentState] );
    }

    void KickCyclicStateControl::setNormalized( float Value )
    {
        if(!std::isfinite(Value)) Value = 0.f;
        auto state = (int)( juce::jlimit(0.f, 1.f, Value) * states.size() );
        setCurrentState( state );
    }

    float KickCyclicStateControl::getNormalized()
    {
        return states.empty() ? 0.0f : (float)currentState / (float)states.size();
    }

    void KickCyclicStateControl::buttonClicked( juce::Button* button )
    {
        if( states.size() == 0 )
            return;

        if( button == &leftNav )
        {
            if( currentState == 0 )
                currentState = (unsigned int)states.size() - 1;
            else
                currentState--;
        }
        else
        {
            if( currentState == (unsigned int)states.size() - 1 )
                currentState = 0;
            else
                currentState++;
        }

        stateChanged( (int)currentState );
        setCurrentState( currentState );
    }

}
