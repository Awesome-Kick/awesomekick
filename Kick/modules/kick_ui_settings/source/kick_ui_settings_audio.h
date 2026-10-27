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

// Module include files.

namespace kick_ui
{
    /// Audio device selection: device type (ASIO, Windows Audio, ALSA, ...), device,
    /// channels, sample rate and buffer size. Changes are saved to the prefs folder
    /// and reopened by the engine at startup.
    class SettingsAudio
        : public SettingsInterface
        , private juce::ChangeListener
    {
    public:
        SettingsAudio();
        ~SettingsAudio() override;

        // Virtual functions
        void refresh() override {}
        void setupHeader( kick_ui::KickModeHeader& ) override {}

        /// Where the device state is stored (shared with the engine's startup code).
        static juce::File getStateFile();

    private:
        void changeListenerCallback( juce::ChangeBroadcaster* ) override;
        void resized() override;
        void paint( juce::Graphics& g ) override;
        void visibilityChanged() override;

        void attach();

        juce::AudioDeviceManager*                             deviceManager{ nullptr };
        std::unique_ptr<juce::AudioDeviceSelectorComponent>   selector;
        juce::Viewport                                        viewport;
        juce::LookAndFeel_V4                                  lookAndFeel;
    };
} // namespace kick_ui
