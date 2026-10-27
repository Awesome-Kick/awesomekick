/*******************************************************************************
 BEGIN_JUCE_MODULE_DECLARATION

  ID:                 AudIO_constants
  vendor:             community
  version:            0.1.0
  name:               AudIO constants (reconstruction)
  description:        Stand-in for the unreleased AudIO_constants module
  website:
  license:            AGPLv3

  dependencies:

 END_JUCE_MODULE_DECLARATION
*******************************************************************************/

/*
 * The original module was not part of the public release. These values are
 * reconstructed from how Kick uses them; the ones marked "guess" are not
 * confirmed against the shipping firmware.
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#pragma once

// Device ALSA config is 48 kHz (Linux/.../etc/asound.conf).
#ifndef AUDIO_SAMPLE_RATE
    #define AUDIO_SAMPLE_RATE 48000
#endif
// Must match BLOCK_SIZE in AudIO/Daw/DawEngine.cpp (the memory pool block):
// ISynthObject renders voices into juce buffers sized from the pool, and
// ISynthVoice::tempBuffer is AUDIO_BLOCK_SIZE long, so smaller values overflow.
#ifndef AUDIO_BLOCK_SIZE
    #define AUDIO_BLOCK_SIZE 480 // 10 ms at 48 kHz
#endif

static constexpr unsigned int AUDIO_NUM_INPUTS       = 2;
static constexpr unsigned int AUDIO_NUM_OUTPUTS      = 2;
static constexpr unsigned int AUDIO_NUM_SEND_RETURNS = 4; // four send gains per pad (sg0..sg3)
static constexpr unsigned int AUDIO_NUM_SUBMIX       = 4; // guess
static constexpr unsigned int AUDIO_NUM_AUX          = 2; // guess

// Sentinel for detached UI attachments.
static constexpr int UNKNOWN_VALUE = -999999;

namespace MIDI
{
    static constexpr int   MIN{ 0 };
    static constexpr int   MAX{ 127 };
    static constexpr int   NUMERIC_MAX{ 128 }; // exclusive bound for note / pad loops
    static constexpr int   STEP{ 1 };
    static constexpr int   DEFAULT{ 0 };         // used for randomisation amounts, so 0
    static constexpr int   LEVEL_DEFAULT{ 100 }; // guess
    static constexpr int   MIDDLE_C{ 60 };
    static constexpr int   NOTE_DEFAULT{ 0 }; // pads are note numbers 0..127 (8 banks x 16), so pad A1
    static constexpr float SCALER{ 1.0f / 127.0f };
} // namespace MIDI

namespace MIDI_CH
{
    static constexpr int MIN{ 1 };
    static constexpr int MAX{ 16 };
    static constexpr int DEFAULT{ 1 };
} // namespace MIDI_CH

namespace POLYPHONY
{
    static constexpr int MIN{ 1 };
    static constexpr int MAX{ 32 }; // guess; voices are pre-allocated per program
    static constexpr int STEP{ 1 };
    static constexpr int DEFAULT{ 16 }; // guess
} // namespace POLYPHONY

// Linear gain.
namespace GAIN
{
    static constexpr float MIN{ 0.0f };
    static constexpr float MAX{ 2.0f }; // guess (+6 dB)
    static constexpr float STEP{ 0.01f };
    static constexpr float DEFAULT{ 1.0f };
    static constexpr float ROOT_2{ 0.70710678f }; // -3 dB master default
} // namespace GAIN

// Matches AudIO::Panner: -1 (left) .. 1 (right).
namespace PAN
{
    static constexpr float MIN{ -1.0f };
    static constexpr float MAX{ 1.0f };
    static constexpr float STEP{ 0.01f };
    static constexpr float DEFAULT{ 0.0f };
} // namespace PAN

namespace DB
{
    static constexpr float MIN{ -60.0f }; // guess; meter floor
    static constexpr float MAX{ 0.0f };
} // namespace DB
