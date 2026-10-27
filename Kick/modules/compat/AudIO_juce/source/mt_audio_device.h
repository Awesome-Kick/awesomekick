/*
 * JUCE audio device for the AudIO engine.
 *
 * The AudIO graph runs in fixed blocks, but JUCE callbacks can be any size,
 * so audio goes through a one-block FIFO (one block of extra latency).
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#pragma once

namespace mt
{
    class AudioDevice
        : public AudIO::IAudioDevice
        , private juce::AudioIODeviceCallback
    {
    public:
        // DeviceStateXml: saved juce::AudioDeviceManager state, or empty for the default device.
        AudioDevice( uint32_t NumInputs, uint32_t NumOutputs, const juce::String& DeviceStateXml );
        ~AudioDevice() override;

        void start( uint32_t SampleRate, uint32_t BlockSize ) override;
        void resume() override;
        void pause() override;
        void stop() override;

        juce::AudioDeviceManager& getJuceAudioDeviceMgr() noexcept { return *deviceManager; }

    private:
        // The engine lives in a function-local static (mt::getAudioInterface), so
        // it is destroyed after JUCE's own singletons. This guard is deleted by
        // JUCE during shutdown and closes the device while JUCE is still alive.
        struct ShutdownGuard : public juce::DeletedAtShutdown
        {
            explicit ShutdownGuard( AudioDevice& D ) : device( D ) {}
            ~ShutdownGuard() override
            {
                device.shutdownGuard = nullptr;
                device.release();
            }
            AudioDevice& device;
        };

        void release();

        void audioDeviceIOCallbackWithContext( const float* const* Inputs, int NumInputs, float* const* Outputs, int NumOutputs, int NumSamples, const juce::AudioIODeviceCallbackContext& ) override;
        void audioDeviceAboutToStart( juce::AudioIODevice* Device ) override;
        void audioDeviceStopped() override;

        void runBlock() noexcept;

        std::unique_ptr<juce::AudioDeviceManager> deviceManager{ std::make_unique<juce::AudioDeviceManager>() };
        ShutdownGuard*                            shutdownGuard{ nullptr };
        const uint32_t                            numIns, numOuts;
        juce::String             stateXml;

        uint32_t                 requestedRate{ 48000 };
        uint32_t                 blockSize{ 128 };
        juce::AudioBuffer<float> inFifo, outFifo;
        int                      fifoPos{ 0 };
    };
} // namespace mt
