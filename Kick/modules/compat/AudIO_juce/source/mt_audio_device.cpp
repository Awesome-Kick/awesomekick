/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

namespace mt
{
    AudioDevice::AudioDevice( uint32_t NumInputs, uint32_t NumOutputs, const juce::String& DeviceStateXml )
        : AudIO::IAudioDevice( NumInputs, NumOutputs )
        , shutdownGuard( new ShutdownGuard( *this ) )
        , numIns( NumInputs )
        , numOuts( NumOutputs )
        , stateXml( DeviceStateXml )
    {
    }

    AudioDevice::~AudioDevice()
    {
        // Normal teardown (before JUCE shuts down): drop the guard ourselves.
        if( auto* guard = std::exchange( shutdownGuard, nullptr ) )
            delete guard;
        release();
    }

    void AudioDevice::release()
    {
        stop();
        deviceManager.reset();
    }

    void AudioDevice::start( uint32_t SampleRate, uint32_t BlockSize )
    {
        requestedRate = SampleRate;
        blockSize     = juce::jmax( 1u, BlockSize );

        inFifo.setSize( (int)numIns, (int)blockSize );
        outFifo.setSize( (int)numOuts, (int)blockSize );
        inFifo.clear();
        outFifo.clear();
        fifoPos = 0;

        // Prepare at the requested rate now; audioDeviceAboutToStart() re-prepares
        // if the hardware ends up running at a different one.
        prepareToPlay( requestedRate, blockSize );

        std::unique_ptr<juce::XmlElement> saved;
        if( stateXml.isNotEmpty() )
            saved = juce::parseXML( stateXml );

        juce::AudioDeviceManager::AudioDeviceSetup preferred;
        preferred.sampleRate = requestedRate;
        preferred.bufferSize = (int)blockSize;

        const auto error = deviceManager->initialise( (int)numIns, (int)numOuts, saved.get(), true, {}, &preferred );
        if( error.isNotEmpty() )
            DBG( "AudioDevice: " << error );

#if defined( SIM_BUILD ) && SIM_BUILD
        // KICKEM_AUDIO_TYPE=<JUCE device type, e.g. JACK>: on a headless server the simulator plays into a
        // JACK server without hardware, from where scripts/kickray-audio.py streams it to the browser.
        if( const auto type = juce::SystemStats::getEnvironmentVariable( "KICKEM_AUDIO_TYPE", {} ); type.isNotEmpty() )
        {
            deviceManager->setCurrentAudioDeviceType( type, true );
            auto* device = deviceManager->getCurrentAudioDevice();
            std::fprintf( stderr, "[Kickray audio] %s: %s\n", type.toRawUTF8(),
                          device ? ( device->getName() + ", " + juce::String( device->getCurrentSampleRate() ) + " Hz, "
                                     + juce::String( device->getCurrentBufferSizeSamples() ) + " samples" ).toRawUTF8()
                                 : "no device (is the server running?)" );
        }
#endif

        deviceManager->addAudioCallback( this );
        setState( AudIO::AudioDeviceState::playing );
    }

    void AudioDevice::resume() { setState( AudIO::AudioDeviceState::playing ); }
    void AudioDevice::pause() { setState( AudIO::AudioDeviceState::paused ); }

    void AudioDevice::stop()
    {
        if( !isInitialized() || !deviceManager )
            return;

        deviceManager->removeAudioCallback( this );
        deviceManager->closeAudioDevice();
        setState( AudIO::AudioDeviceState::stopped );
        teardown();
    }

    void AudioDevice::audioDeviceAboutToStart( juce::AudioIODevice* Device )
    {
        const auto rate = (uint32_t)Device->getCurrentSampleRate();
        if( rate != requestedRate && rate > 0 )
        {
            DBG( "AudioDevice: running at " << (int)rate << " Hz instead of " << (int)requestedRate );
            requestedRate = rate;
            prepareToPlay( rate, blockSize );
        }
        inFifo.clear();
        outFifo.clear();
        fifoPos = 0;
    }

    void AudioDevice::audioDeviceStopped() {}

    void AudioDevice::runBlock() noexcept
    {
        for( uint32_t ch = 0; ch < numIns; ++ch )
            if( auto* dst = _buffer.getInputBuffer( ch ) )
                juce::FloatVectorOperations::copy( dst, inFifo.getReadPointer( (int)ch ), (int)blockSize );

        _buffer.clearOutputs();
        if( isPlaying() )
            process( _buffer );

        for( uint32_t ch = 0; ch < numOuts; ++ch )
        {
            if( auto* src = _buffer.getOutputBuffer( ch ) )
                outFifo.copyFrom( (int)ch, 0, src, (int)blockSize );
            else
                outFifo.clear( (int)ch, 0, (int)blockSize );
        }
    }

    void AudioDevice::audioDeviceIOCallbackWithContext( const float* const* Inputs, int NumInputs, float* const* Outputs, int NumOutputs, int NumSamples, const juce::AudioIODeviceCallbackContext& )
    {
        // Covers the whole engine: decaying filters and reverbs otherwise produce denormals.
        const juce::ScopedNoDenormals noDenormals;

        // Direct path: the device delivers exactly one engine block, so the FIFO would
        // only add a block of latency.
        if( fifoPos == 0 && NumSamples == (int)blockSize )
        {
            for( int ch = 0; ch < (int)numIns; ++ch )
            {
                auto* dst = inFifo.getWritePointer( ch );
                if( ch < NumInputs && Inputs[ch] )
                    juce::FloatVectorOperations::copy( dst, Inputs[ch], NumSamples );
                else
                    juce::FloatVectorOperations::clear( dst, NumSamples );
            }
            runBlock();
            for( int ch = 0; ch < NumOutputs; ++ch )
                if( Outputs[ch] )
                {
                    if( ch < (int)numOuts )
                        juce::FloatVectorOperations::copy( Outputs[ch], outFifo.getReadPointer( ch ), NumSamples );
                    else
                        juce::FloatVectorOperations::clear( Outputs[ch], NumSamples );
                }
            outFifo.clear(); // already played: don't replay it if the FIFO path takes over
            return;
        }

        int done = 0;
        while( done < NumSamples )
        {
            const int n = juce::jmin( NumSamples - done, (int)blockSize - fifoPos );

            for( int ch = 0; ch < (int)numIns; ++ch )
            {
                auto* dst = inFifo.getWritePointer( ch, fifoPos );
                if( ch < NumInputs && Inputs[ch] )
                    juce::FloatVectorOperations::copy( dst, Inputs[ch] + done, n );
                else
                    juce::FloatVectorOperations::clear( dst, n );
            }

            for( int ch = 0; ch < NumOutputs; ++ch )
            {
                if( !Outputs[ch] )
                    continue;
                if( ch < (int)numOuts )
                    juce::FloatVectorOperations::copy( Outputs[ch] + done, outFifo.getReadPointer( ch, fifoPos ), n );
                else
                    juce::FloatVectorOperations::clear( Outputs[ch] + done, n );
            }

            fifoPos += n;
            done += n;
            if( fifoPos >= (int)blockSize )
            {
                runBlock();
                fifoPos = 0;
            }
        }
    }
} // namespace mt
