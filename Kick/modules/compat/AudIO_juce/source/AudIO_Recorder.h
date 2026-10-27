/*
 * Recorder objects for the Sampler and Looper screens.
 *
 * Both run inside the AudIO graph as stereo objects fed from the default
 * stereo input. Buffers are resized on the message thread under a spin lock;
 * the audio thread only try-locks it and outputs silence for that block if it
 * can't get it.
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#pragma once

namespace AudIO
{
    class Recorder
        : public Object
        , public DawRouting
        , public DawInserts
        , private juce::AsyncUpdater
    {
    public:
        enum Direction
        {
            FORWARD,
            REVERSE,
        };

        // Which inputs are recorded, and into how many channels.
        enum Mode
        {
            L_MONO,
            R_MONO,
            LR_MONO,
            L_STEREO,
            R_STEREO,
            LR_STEREO,
        };

        Recorder( Design& D, const std::string& Name );
        ~Recorder() override;

        const char* getType() const override { return "Recorder"; }

        void setEnable( bool Enable ) noexcept { enabled.store( Enable ); }
        bool isEnabled() const noexcept { return enabled.load(); }

        void  setMonitorOn( bool Monitor ) noexcept { monitor.store( Monitor ); }
        bool  isMonitorOn() const noexcept { return monitor.load(); }
        void  setMonitorGain( float Gain ) noexcept { monitorGain.store( Gain ); }
        float getMonitorGain() const noexcept { return monitorGain.load(); }

        void  setGain( float Gain ) noexcept { gain.store( juce::jmax( 0.f, Gain ) ); }
        void  setGainDB( float GainDB ) noexcept { setGain( juce::Decibels::decibelsToGain( GainDB ) ); }
        float getGain() const noexcept { return gain.load(); }
        void  setPan( float Pan ) noexcept { pan.store( juce::jlimit( -1.f, 1.f, Pan ) ); }
        float getPan() const noexcept { return pan.load(); }
        void  setGainAndPan( float Gain, float Pan, bool /*Immediate*/ = true ) noexcept
        {
            setGain( Gain );
            setPan( Pan );
        }

        // Input level (linear, 0..1) that has to be crossed before recording starts.
        void  setThreshold( float Threshold ) noexcept { threshold.store( juce::jlimit( 0.f, 1.f, Threshold ) ); }
        void  setThresholdDB( float DB ) noexcept { setThreshold( juce::Decibels::decibelsToGain( DB, -100.f ) ); }
        float getThreshold() const noexcept { return threshold.load(); }
        float getThresholdDB() const noexcept { return juce::Decibels::gainToDecibels( threshold.load(), -100.f ); }

        void      setDirection( Direction Dir ) noexcept { direction.store( Dir ); }
        Direction getDirection() const noexcept { return direction.load(); }
        bool      isForward() const noexcept { return FORWARD == direction.load(); }
        bool      isReverse() const noexcept { return REVERSE == direction.load(); }

        void setRecordingMode( Mode M ) noexcept { mode.store( juce::jlimit( L_MONO, LR_STEREO, M ) ); }
        Mode getRecordingMode() const noexcept { return mode.load(); }

        void setLooping( bool Looping ) noexcept { looping.store( Looping ); }
        bool isLooping() const noexcept { return looping.load(); }
        virtual void setOverdub( bool Overdub ) noexcept { overdub.store( Overdub ); }
        bool isOverdubbing() const noexcept { return overdub.load(); }

        // Resizes the buffer, keeping what was already recorded.
        void   setLength( double TimeInSeconds );
        double getLength() const noexcept { return lengthInSeconds; }

        MeterData readInputMeter();
        MeterData readOutputMeter();

        // Recorded audio. Only touch it from the message thread.
        juce::AudioBuffer<float>& getAudioBuffer() noexcept { return buffer; }

        // Writes the recorded part of the buffer as a 24-bit WAV file.
        bool saveToFile( const juce::File& File );

    protected:
        static int  numChannelsFor( Mode M ) noexcept { return ( L_STEREO <= M ) ? 2 : 1; }
        static bool fileHasAudio( const juce::AudioBuffer<float>& B ) noexcept { return 0 < B.getNumSamples(); }

        void prepareToPlay( uint32_t SampleRate, uint32_t BlockSize ) override;

        // Fills Left/Right with the (gain-applied) input frames for the current mode.
        void readInputs( Buffer<float>& Buf, int NumSamples, float* Left, float* Right, float GainL, float GainR ) const noexcept;
        bool inputCrossesThreshold( Buffer<float>& Buf, int NumSamples ) const noexcept;
        void writeMonitor( Buffer<float>& Buf, int NumSamples ) const noexcept;
        void panGains( float& GainL, float& GainR ) const noexcept;

        // Runs on the message thread after the audio thread calls triggerAsyncUpdate().
        virtual void onAudioThreadEvent() {}
        void         postFromAudioThread() { triggerAsyncUpdate(); }

        juce::SpinLock           bufferLock;
        juce::AudioBuffer<float> buffer;
        std::vector<float>       scratchL, scratchR;
        double                   lengthInSeconds{ 0 };

        std::atomic<bool>      enabled{ true };
        std::atomic<bool>      monitor{ false };
        std::atomic<float>     monitorGain{ 1.f };
        std::atomic<float>     gain{ 1.f };
        std::atomic<float>     pan{ 0.f };
        std::atomic<float>     threshold{ 0.f };
        std::atomic<Direction> direction{ FORWARD };
        std::atomic<Mode>      mode{ LR_STEREO };
        std::atomic<bool>      looping{ false };
        std::atomic<bool>      overdub{ false };

        LevelMeter metering;
        MeterData  inMeter;
        MeterData  outMeter;

    private:
        void handleAsyncUpdate() override { onAudioThreadEvent(); }
    };

    /// One-shot recorder behind the Sampler screen.
    class SampleRecorder : public Recorder
    {
    public:
        explicit SampleRecorder( Design& D );

        std::function<void()>                       onRecorderStarted;
        std::function<void()>                       onRecorderStopped;
        std::function<void()>                       onRecorderPaused;
        std::function<void()>                       onRecorderResumed;
        std::function<void()>                       onRecorderPreviewStarted;
        std::function<void()>                       onRecorderPreviewStopped;
        std::function<void( double TimeInSeconds )> onRecorderLengthChanged;

        void reset();
        void startRecording( bool FromBeginning );
        void stopRecording();
        void startPreview( double FromTimeInSeconds );
        void stopPreview();
        void saveRecording( const std::string& FilePath );

        bool  isRecording() const noexcept { return recording.load(); }
        bool  isPreviewing() const noexcept { return previewing.load(); }
        float readProgress() const noexcept;

    protected:
        void process( Buffer<float>& Buf ) override;
        void onAudioThreadEvent() override;

    private:
        void resetWritePosition() noexcept;

        std::atomic<bool> recording{ false };
        std::atomic<bool> previewing{ false };
        std::atomic<bool> thresholdCrossed{ false };
        std::atomic<bool> finishedRecording{ false };
        std::atomic<bool> finishedPreview{ false };
        std::atomic<int>  writePos{ 0 };
        std::atomic<int>  readPos{ 0 };
        std::atomic<int>  recordedFrom{ 0 }; // first written sample, for reverse recording
        std::atomic<int>  recordedTo{ 0 };   // one past the last written sample
    };

    /// Bar-length loop recorder behind the Looper screen. Gain and pan act on playback.
    class Looper : public Recorder
    {
    public:
        explicit Looper( Design& D );

        void setRecord( bool Record ) noexcept;
        void setOverdub( bool Overdub ) noexcept override;
        void setPlay() noexcept;

        // Starts the loop running. Recording begins once the input crosses the threshold, unless skipped.
        void start( bool FromBeginning, bool SkipThresholdDetection );
        void play( double FromTimeInSeconds = 0.0 );
        void stop() override;
        void clear();
        void undo();
        void redo();
        void save( const juce::String& FilePath );

        bool    isEmpty() const noexcept { return empty.load(); }
        bool    isRunning() const noexcept { return running.load(); }
        bool    isRecording() const noexcept { return running.load() && record.load(); }
        bool    isPreviewing() const noexcept { return running.load() && !record.load() && !overdub.load(); }
        double& getProgress() noexcept;

    protected:
        void process( Buffer<float>& Buf ) override;

    private:
        void takeUndoSnapshot();

        juce::AudioBuffer<float> undoBuffer;
        bool                     hasUndo{ false };
        bool                     undone{ false };
        double                   progress{ 0 };

        std::atomic<bool> running{ false };
        std::atomic<bool> record{ false };
        std::atomic<bool> empty{ true };
        std::atomic<bool> thresholdCrossed{ false };
        std::atomic<int>  pos{ 0 };
    };
} // namespace AudIO
