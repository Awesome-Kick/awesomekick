/*
 * Per-voice filter and envelope used by mt::DrumVoice.
 *
 * Reconstructed from how drum_voice.cpp calls them. The filter types follow
 * the list in kick_common_defs.h (filterTypes); "Model" and "Kick3000" types
 * are approximated with a driven low-pass, since the originals are unknown.
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#pragma once

namespace mt
{
    // Same order as filterTypes in kick_ui_common/source/kick_common_defs.h.
    enum class FilterType
    {
        LowPass1,
        LowPass2,
        LowPass4,
        LowPass6,
        LowPass8,
        HighPass1,
        HighPass2,
        HighPass4,
        HighPass6,
        HighPass8,
        BandPass2,
        BandPass4,
        BandPass6,
        BandPass8,
        BandStop2,
        BandStop4,
        BandStop6,
        BandStop8,
        BandBoost2,
        BandBoost4,
        BandBoost6,
        BandBoost8,
        Model1,
        Model2,
        Model3,
        Model4,
        Kick3000LPF,
    };

    /// Cascaded TPT state-variable filter, processed one stereo frame at a time.
    class Filter
    {
    public:
        // Cutoff in Hz; setCutoff() takes the normalised (0..1) value.
        static inline const juce::NormalisableRange<float> freqRange = []() {
            juce::NormalisableRange<float> r{ 20.f, 20000.f };
            r.setSkewForCentre( 1000.f );
            return r;
        }();

        void setSampleRate( double SampleRate ) noexcept
        {
            sampleRate = SampleRate;
            dirty      = true;
        }

        void setFilterType( FilterType Type ) noexcept
        {
            if( type != Type )
            {
                type = Type;
                reset();
            }
        }

        // 0..100 (UI percentage).
        void setResonance( double Resonance ) noexcept
        {
            const auto r = juce::jlimit( 0.0, 0.97, Resonance * 0.01 );
            if( r != resonance )
            {
                resonance = r;
                dirty     = true;
            }
        }

        // Normalised cutoff; values outside 0..1 (from modulation) are clamped.
        void setCutoff( double Normalised ) noexcept
        {
            const auto n = juce::jlimit( 0.0, 1.0, Normalised );
            if( std::abs( n - cutoff ) > 1.0e-5 )
            {
                cutoff = n;
                dirty  = true;
            }
        }

        void reset() noexcept
        {
            for( auto& ch : state )
                for( auto& s : ch )
                    s = {};
        }

        // Processes the first sample of each channel (up to two) in place.
        void process( juce::AudioBuffer<float>& Frame ) noexcept
        {
            if( dirty )
                recalculate();

            const int numCh = juce::jmin( 2, Frame.getNumChannels() );
            for( int ch = 0; ch < numCh; ++ch )
            {
                auto* d = Frame.getWritePointer( ch );
                d[0]    = processSample( ch, d[0] );
            }
        }

    private:
        struct Stage
        {
            float ic1{ 0 }, ic2{ 0 };
        };

        enum class Shape
        {
            LowPass,
            HighPass,
            BandPass,
            BandStop,
            BandBoost
        };

        struct Topology
        {
            Shape shape;
            int   poles; // 1 = one-pole, otherwise a multiple of 2
            bool  drive;
        };

        static Topology topologyFor( FilterType T ) noexcept
        {
            switch( T )
            {
                case FilterType::LowPass1: return { Shape::LowPass, 1, false };
                case FilterType::LowPass2: return { Shape::LowPass, 2, false };
                case FilterType::LowPass4: return { Shape::LowPass, 4, false };
                case FilterType::LowPass6: return { Shape::LowPass, 6, false };
                case FilterType::LowPass8: return { Shape::LowPass, 8, false };
                case FilterType::HighPass1: return { Shape::HighPass, 1, false };
                case FilterType::HighPass2: return { Shape::HighPass, 2, false };
                case FilterType::HighPass4: return { Shape::HighPass, 4, false };
                case FilterType::HighPass6: return { Shape::HighPass, 6, false };
                case FilterType::HighPass8: return { Shape::HighPass, 8, false };
                case FilterType::BandPass2: return { Shape::BandPass, 2, false };
                case FilterType::BandPass4: return { Shape::BandPass, 4, false };
                case FilterType::BandPass6: return { Shape::BandPass, 6, false };
                case FilterType::BandPass8: return { Shape::BandPass, 8, false };
                case FilterType::BandStop2: return { Shape::BandStop, 2, false };
                case FilterType::BandStop4: return { Shape::BandStop, 4, false };
                case FilterType::BandStop6: return { Shape::BandStop, 6, false };
                case FilterType::BandStop8: return { Shape::BandStop, 8, false };
                case FilterType::BandBoost2: return { Shape::BandBoost, 2, false };
                case FilterType::BandBoost4: return { Shape::BandBoost, 4, false };
                case FilterType::BandBoost6: return { Shape::BandBoost, 6, false };
                case FilterType::BandBoost8: return { Shape::BandBoost, 8, false };
                case FilterType::Model1:
                case FilterType::Model2:
                case FilterType::Model3:
                case FilterType::Model4: return { Shape::LowPass, 4, true };
                case FilterType::Kick3000LPF: return { Shape::LowPass, 2, true };
            }
            return { Shape::LowPass, 2, false };
        }

        void recalculate() noexcept
        {
            const auto hz = juce::jlimit( 20.0, 0.49 * sampleRate, (double)freqRange.convertFrom0to1( (float)cutoff ) );
            g             = (float)std::tan( juce::MathConstants<double>::pi * hz / sampleRate );
            k             = (float)( 2.0 * ( 1.0 - resonance ) );
            a1            = 1.f / ( 1.f + g * ( g + k ) );
            a1Flat        = 1.f / ( 1.f + g * ( g + flatK ) );
            onePoleG      = g / ( 1.f + g );
            dirty         = false;
        }

        float processSample( int ch, float x ) noexcept
        {
            const auto topo = topologyFor( type );
            if( topo.drive )
                x = std::tanh( 1.5f * x );

            if( 1 == topo.poles )
            {
                auto&      s  = state[ch][0];
                const auto v  = ( x - s.ic1 ) * onePoleG;
                const auto lp = v + s.ic1;
                s.ic1         = lp + v;
                return ( Shape::HighPass == topo.shape ) ? x - lp : lp;
            }

            // Only the last stage resonates; the others are flat, so stacking
            // stages steepens the slope without multiplying the resonant peak.
            const int stages = juce::jmin( maxStages, topo.poles / 2 );
            for( int i = 0; i < stages; ++i )
            {
                const bool last   = ( i == stages - 1 );
                const auto stageK = last ? k : flatK;
                const auto stageA = last ? a1 : a1Flat;

                auto&      s  = state[ch][i];
                const auto v3 = x - s.ic2;
                const auto v1 = stageA * ( s.ic1 + g * v3 );
                const auto v2 = s.ic2 + g * v1;
                s.ic1         = 2.f * v1 - s.ic1;
                s.ic2         = 2.f * v2 - s.ic2;

                const auto lp = v2;
                const auto bp = stageK * v1; // unity gain at the centre frequency
                const auto hp = x - stageK * v1 - v2;
                switch( topo.shape )
                {
                    case Shape::LowPass: x = lp; break;
                    case Shape::HighPass: x = hp; break;
                    case Shape::BandPass: x = bp; break;
                    case Shape::BandStop: x = lp + hp; break;
                    case Shape::BandBoost: x = x + bp; break;
                }
            }
            return topo.drive ? std::tanh( x ) : x;
        }

        static constexpr int   maxStages = 4;
        static constexpr float flatK     = 1.41421356f; // Butterworth damping

        FilterType type{ FilterType::LowPass2 };
        double     sampleRate{ 48000 };
        double     cutoff{ 1.0 };
        double     resonance{ 0.0 };
        bool       dirty{ true };
        float      g{ 0 }, k{ 2 }, a1{ 1 }, a1Flat{ 1 }, onePoleG{ 0 };

        std::array<std::array<Stage, maxStages>, 2> state{};
    };

    /// Linear attack-hold-decay-sustain-release envelope. Times are in samples.
    class ADSR
    {
    public:
        void setSampleRate( double ) noexcept {}

        void setParameters( double Attack, double Hold, double Decay, double Release, double Sustain ) noexcept
        {
            attack  = juce::jmax( 0.0, Attack );
            hold    = juce::jmax( 0.0, Hold );
            decay   = juce::jmax( 0.0, Decay );
            release = juce::jmax( 0.0, Release );
            sustain = juce::jlimit( 0.0, 1.0, Sustain );
        }

        void gate( int On ) noexcept
        {
            if( On )
            {
                stage   = Stage::Attack;
                counter = 0;
                if( attack <= 0 )
                    enterHold();
            }
            else if( Stage::Idle != stage )
            {
                stage        = Stage::Release;
                releaseStart = level;
                counter      = 0;
                if( release <= 0 )
                    enterIdle();
            }
        }

        bool isActive() const noexcept { return Stage::Idle != stage; }

        float operator()() noexcept
        {
            switch( stage )
            {
                case Stage::Idle: break;
                case Stage::Attack:
                    level = (float)( ++counter / attack );
                    if( counter >= attack ) enterHold();
                    break;
                case Stage::Hold:
                    if( ++counter >= hold ) enterDecay();
                    break;
                case Stage::Decay:
                    level = (float)( 1.0 - ( 1.0 - sustain ) * ( ++counter / decay ) );
                    if( counter >= decay ) enterSustain();
                    break;
                case Stage::Sustain: break;
                case Stage::Release:
                    level = (float)( releaseStart * ( 1.0 - ( ++counter / release ) ) );
                    if( counter >= release ) enterIdle();
                    break;
            }
            return level;
        }

    private:
        enum class Stage
        {
            Idle,
            Attack,
            Hold,
            Decay,
            Sustain,
            Release
        };

        void enterHold() noexcept
        {
            level   = 1.f;
            stage   = Stage::Hold;
            counter = 0;
            if( hold <= 0 ) enterDecay();
        }
        void enterDecay() noexcept
        {
            stage   = Stage::Decay;
            counter = 0;
            if( decay <= 0 ) enterSustain();
        }
        void enterSustain() noexcept
        {
            level = (float)sustain;
            stage = Stage::Sustain;
        }
        void enterIdle() noexcept
        {
            level = 0.f;
            stage = Stage::Idle;
        }

        Stage  stage{ Stage::Idle };
        double attack{ 0 }, hold{ 0 }, decay{ 0 }, release{ 0 }, sustain{ 1 };
        double counter{ 0 };
        float  level{ 0 }, releaseStart{ 0 };
    };
} // namespace mt
