/*******************************************************************************
 BEGIN_JUCE_MODULE_DECLARATION

  ID:                 AudIO_juce
  vendor:             community
  version:            0.1.0
  name:               AudIO JUCE extensions (reconstruction)
  description:        Sample recorder and looper objects for AudIO, plus the JUCE audio modules Kick expects
  website:
  license:            AGPLv3

  dependencies:       AudIO, juce_audio_basics, juce_audio_devices, juce_audio_formats, juce_audio_utils, juce_dsp

 END_JUCE_MODULE_DECLARATION
*******************************************************************************/

/*
 * The original AudIO_juce module was not part of the public release. Kick
 * needs it for the JUCE audio modules and for the Recorder / SampleRecorder /
 * Looper objects behind the Sampler and Looper screens. These are a fresh
 * implementation written against the API Kick calls.
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#pragma once

#include <AudIO/AudIO.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>

#include <AudIO/Daw/DawRouting.h>
#include <AudIO/Objects/LevelMeter.h>

#include "source/AudIO_Recorder.h"
#include "source/mt_voice_dsp.h"
#include "source/mt_audio_device.h"

// Kick's kick_Constants.h declares EMPTY::VTState; these were declared here.
namespace EMPTY
{
    static const juce::String             String{};
    static const juce::AudioBuffer<float> SampleBuffer{};
} // namespace EMPTY

namespace mt
{
    using Recorder       = AudIO::Recorder;
    using SampleRecorder = AudIO::SampleRecorder;
    using Looper         = AudIO::Looper;
} // namespace mt
