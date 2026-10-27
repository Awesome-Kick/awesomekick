/*
 * Copyright (c) 2024 MUSIC Tribe
 *
 * This file is part of Kick.
 *
 * Kick is free software: you can redistribute it and/or modify it under the
 * terms of the GNU Affero General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * Kick is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for
 * more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with Kick. If not, see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#pragma once

#include "kick_ui_settings_audio.h"

namespace kick_ui
{
    SettingsAudio::SettingsAudio()
        : SettingsInterface( 1, 1, false )
    {
        ui::KickGrid::setBackgroundColour( juce::Colours::transparentBlack );

        // The device selector is a stock JUCE component; give it a dark scheme that
        // matches the Kick screens rather than the app's custom look and feel.
        lookAndFeel.setColourScheme( juce::LookAndFeel_V4::getMidnightColourScheme() );
        viewport.setScrollBarsShown( true, false );
        addAndMakeVisible( viewport );
    }

    SettingsAudio::~SettingsAudio()
    {
        if( deviceManager )
            deviceManager->removeChangeListener( this );
        viewport.setViewedComponent( nullptr, false );
        selector = nullptr;
    }

    juce::File SettingsAudio::getStateFile() { return getAppDirs( MTPrefs ).getChildFile( "audio_device.xml" ); }

    void SettingsAudio::attach()
    {
        // The engine opens its device asynchronously at startup, so create the
        // selector the first time this page is shown rather than in the constructor.
        if( selector )
            return;

        deviceManager = &mt::getAudioInterface().getAudioDeviceManager();
        selector      = std::make_unique<juce::AudioDeviceSelectorComponent>( *deviceManager,
                                                                         0, 2,     // inputs
                                                                         2, 2,     // outputs
                                                                         false,    // MIDI inputs: see the MIDI page
                                                                         false,    // MIDI outputs
                                                                         true,     // stereo pairs
                                                                         false );  // show advanced options
        selector->setLookAndFeel( &lookAndFeel );
        viewport.setViewedComponent( selector.get(), false );
        deviceManager->addChangeListener( this );
        resized();
    }

    void SettingsAudio::changeListenerCallback( juce::ChangeBroadcaster* )
    {
        if( !deviceManager )
            return;
        if( auto xml = deviceManager->createStateXml() )
        {
            auto file = getStateFile();
            file.getParentDirectory().createDirectory();
            xml->writeTo( file );
        }
    }

    void SettingsAudio::visibilityChanged()
    {
        if( isVisible() )
            attach();
    }

    void SettingsAudio::paint( juce::Graphics& g )
    {
        g.setColour( ui::Theme::backgroundColour( 1 ) );
        g.fillRoundedRectangle( getLocalBounds().toFloat(), 5.f );
    }

    void SettingsAudio::resized()
    {
        viewport.setBounds( getLocalBounds().reduced( 10 ) );
        if( selector )
            selector->setSize( viewport.getWidth() - viewport.getScrollBarThickness(), juce::jmax( 420, viewport.getHeight() ) );
    }
} // namespace kick_ui
