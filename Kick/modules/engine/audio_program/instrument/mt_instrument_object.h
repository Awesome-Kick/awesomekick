/*
 * Audio-graph object for a Plugin ("instrument") program: hosts one
 * IInstrumentEngine, applies the program's gain/pan/mute and inserts, and
 * routes to the program's output bus like the other program types.
 *
 * Notes and parameter changes arrive on non-audio threads (UI, MIDI input,
 * sequencer) and are queued, then applied at the start of the next block.
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#pragma once

namespace mt
{
    class MTIProgram;

    class InstrumentObject
        : public AudIO::Object
        , public AudIO::DawRouting
        , public AudIO::DawInserts
    {
    public:
        InstrumentObject( AudIO::Design& D, const std::string& Name );
        ~InstrumentObject() override;

        const char* getType() const override { return "Instrument"; }

        void setProgram( MTIProgram* Program );

        /// Message thread. The engine is prepared here, then swapped in under a lock;
        /// the previous engine is destroyed on this thread.
        void setEngine( std::unique_ptr<IInstrumentEngine> Engine );
        bool hasEngine() const { return engine != nullptr; }
        /// Message thread only (the thread that swaps engines): patch and preset calls.
        IInstrumentEngine* getEngine() const { return engine.get(); }

        // Any thread.
        void noteOn( int Note, int Velocity );
        void noteOff( int Note );
        void allNotesOff();
        void setParam( int Index, float Value );
        void pitchBend( float Semitones );
        void modWheel( float Amount ); // 0..1

        MTMeterData& readOutMeter() { return meter.getMeter( outMeterData ); }

    private:
        void prepareToPlay( uint32_t SampleRate, uint32_t BlockSize ) override;
        void process( AudIO::Buffer<float>& Buffer ) override;

        struct Event
        {
            enum class Type
            {
                NoteOn,
                NoteOff,
                AllNotesOff,
                Param,
                PitchBend,
                ModWheel
            } type;
            int   index;
            float value;
        };

        void push( const Event& E );
        void applyEvents();
        void releaseNote( int InputNote );

        // Audio thread only.
        std::array<int, 128> sounding = []() { std::array<int, 128> a{}; a.fill( -1 ); return a; }(); // input note -> note sent
        std::array<int, 64>  deferredOffs{};
        int                  numDeferredOffs{ 0 };
        juce::LinearSmoothedValue<float> gainL{ 1.f }, gainR{ 1.f };

        static constexpr int  queueSize = 1024;
        juce::AbstractFifo    fifo{ queueSize };
        std::array<Event, queueSize> events{};
        juce::CriticalSection pushLock; // several producer threads

        juce::SpinLock                     engineLock;
        std::unique_ptr<IInstrumentEngine> engine;
        std::atomic<bool>                  prepared{ false };

        MTIAtomicProgramMaster master;
        AudIO::LevelMeter      meter;
        AudIO::MeterData       outMeterData;
        AudIO::Design&         design;
    };
} // namespace mt
