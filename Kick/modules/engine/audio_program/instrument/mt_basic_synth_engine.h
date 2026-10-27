/*
 * "Basic Synth": the built-in reference instrument engine. Eight voices, two
 * PolyBLEP oscillators (saw/square morph, detune) plus a sub, a resonant
 * low-pass with its own envelope, and an amp envelope.
 *
 * It is deliberately self-contained: a ported engine should look like this.
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#pragma once

namespace mt
{
    class BasicSynthEngine : public IInstrumentEngine
    {
    public:
        enum Param
        {
            kWave,
            kDetune,
            kSub,
            kCutoff,
            kResonance,
            kEnvAmount,
            kFilterDecay,
            kAttack,
            kDecay,
            kSustain,
            kRelease,
            kLevel,
            kNumParams
        };

        BasicSynthEngine();

        void prepare( double SampleRate, int MaxBlockSize ) override;
        void reset() override;
        void noteOn( int Note, float Velocity ) override;
        void noteOff( int Note ) override;
        void allNotesOff() override;
        void pitchBend( float Semitones ) override;
        void modWheel( float Amount ) override; // opens the filter up to 3 octaves
        void process( float* const* Out, int NumChannels, int NumSamples ) override;

        const std::vector<InstrumentParam>& getParams() const override { return params; }
        void                                setParam( int Index, float Value ) override;

    private:
        struct Envelope
        {
            enum class Stage
            {
                Idle,
                Attack,
                Decay,
                Sustain,
                Release
            };

            void  start() { stage = Stage::Attack; }
            void  release() { if( stage != Stage::Idle ) stage = Stage::Release; }
            void  kill() { stage = Stage::Idle; level = 0.f; }
            bool  active() const { return stage != Stage::Idle; }
            float next( float AttackInc, float DecayCoef, float Sustain, float ReleaseCoef );

            Stage stage{ Stage::Idle };
            float level{ 0.f };
        };

        struct Voice
        {
            int      note{ -1 };
            float    velocity{ 0.f };
            bool     gate{ false };
            uint64_t age{ 0 };
            double   phase1{ 0 }, phase2{ 0 }, phaseSub{ 0 };
            float    ic1{ 0 }, ic2{ 0 }; // filter state
            Envelope amp, filter;
        };

        Voice* findVoiceFor( int Note );

        std::vector<InstrumentParam> params;
        std::array<float, kNumParams> values{};

        static constexpr int     numVoices = 8;
        std::array<Voice, numVoices> voices{};
        uint64_t                 noteCounter{ 0 };
        double                   sampleRate{ 48000 };
        juce::SmoothedValue<float> smoothCutoff{ 2000.f };
        juce::SmoothedValue<float> smoothLevel{ 0.7f };
        std::vector<float>         cutoffBlock, levelBlock; // per-block scratch, sized in prepare()
        float                      bendSemitones{ 0.f };
        float                      modAmount{ 0.f };
    };
} // namespace mt
