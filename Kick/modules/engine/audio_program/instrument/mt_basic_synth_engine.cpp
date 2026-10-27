/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

namespace mt
{
    namespace
    {
        // PolyBLEP residual for a discontinuity at phase 0 (phase and increment in cycles).
        inline float polyBlep( double t, double dt )
        {
            if( t < dt )
            {
                t /= dt;
                return (float)( t + t - t * t - 1.0 );
            }
            if( t > 1.0 - dt )
            {
                t = ( t - 1.0 ) / dt;
                return (float)( t * t + t + t + 1.0 );
            }
            return 0.f;
        }

        inline float sawBlep( double phase, double dt ) { return (float)( 2.0 * phase - 1.0 ) - polyBlep( phase, dt ); }

        inline float squareBlep( double phase, double dt )
        {
            float v = phase < 0.5 ? 1.f : -1.f;
            v += polyBlep( phase, dt );
            v -= polyBlep( std::fmod( phase + 0.5, 1.0 ), dt );
            return v;
        }

        inline double noteToHz( double Note ) { return 440.0 * std::pow( 2.0, ( Note - 69.0 ) / 12.0 ); }

        // One-pole coefficient that decays to ~0.1% in `Seconds`.
        inline float decayCoef( float Seconds, double SampleRate )
        {
            return (float)std::exp( -6.9 / juce::jmax( 1.0, Seconds * SampleRate ) );
        }
    } // namespace

    float BasicSynthEngine::Envelope::next( float AttackInc, float DecayCoef, float Sustain, float ReleaseCoef )
    {
        switch( stage )
        {
            case Stage::Idle: break;
            case Stage::Attack:
                level += AttackInc;
                if( level >= 1.f )
                {
                    level = 1.f;
                    stage = Stage::Decay;
                }
                break;
            case Stage::Decay:
                level = Sustain + ( level - Sustain ) * DecayCoef;
                if( std::abs( level - Sustain ) < 1.0e-4f )
                    stage = Stage::Sustain;
                break;
            case Stage::Sustain: level = Sustain; break;
            case Stage::Release:
                level *= ReleaseCoef;
                if( level < 1.0e-4f )
                    kill();
                break;
        }
        return level;
    }

    BasicSynthEngine::BasicSynthEngine()
    {
        params = {
            { "wave", "Wave", "", 0.f, 1.f, 0.f },
            { "detune", "Detune", "ct", 0.f, 50.f, 8.f },
            { "sub", "Sub", "", 0.f, 1.f, 0.3f },
            { "cutoff", "Cutoff", "Hz", 30.f, 18000.f, 1800.f },
            { "resonance", "Resonance", "", 0.f, 1.f, 0.25f },
            { "envAmount", "Filter Env", "", 0.f, 1.f, 0.45f },
            { "filterDecay", "Filter Decay", "s", 0.005f, 4.f, 0.35f },
            { "attack", "Attack", "s", 0.001f, 4.f, 0.005f },
            { "decay", "Decay", "s", 0.005f, 4.f, 0.4f },
            { "sustain", "Sustain", "", 0.f, 1.f, 0.6f },
            { "release", "Release", "s", 0.005f, 6.f, 0.3f },
            { "level", "Level", "", 0.f, 1.f, 0.7f },
        };
        jassert( params.size() == kNumParams );
        for( int i = 0; i < kNumParams; ++i )
            values[(size_t)i] = params[(size_t)i].defaultValue;
    }

    void BasicSynthEngine::prepare( double SampleRate, int )
    {
        sampleRate = SampleRate > 0 ? SampleRate : 48000.0;
        smoothCutoff.reset( sampleRate, 0.02 );
        smoothCutoff.setCurrentAndTargetValue( values[kCutoff] );
        reset();
    }

    void BasicSynthEngine::reset()
    {
        for( auto& v : voices )
            v = Voice{};
    }

    void BasicSynthEngine::setParam( int Index, float Value )
    {
        if( !juce::isPositiveAndBelow( Index, (int)kNumParams ) )
            return;
        const auto& p          = params[(size_t)Index];
        values[(size_t)Index] = juce::jlimit( p.minValue, p.maxValue, Value );
        if( Index == kCutoff )
            smoothCutoff.setTargetValue( values[kCutoff] );
    }

    BasicSynthEngine::Voice* BasicSynthEngine::findVoiceFor( int Note )
    {
        // Retrigger the same note, else a free voice, else steal the oldest.
        for( auto& v : voices )
            if( v.note == Note && v.amp.active() )
                return &v;
        for( auto& v : voices )
            if( !v.amp.active() )
                return &v;
        return &*std::min_element( voices.begin(), voices.end(), []( const Voice& a, const Voice& b ) { return a.age < b.age; } );
    }

    void BasicSynthEngine::noteOn( int Note, float Velocity )
    {
        auto* v = findVoiceFor( Note );
        if( v->note != Note )
        {
            v->phase1 = v->phase2 = v->phaseSub = 0;
            v->ic1 = v->ic2 = 0;
        }
        v->note     = Note;
        v->velocity = juce::jlimit( 0.f, 1.f, Velocity );
        v->gate     = true;
        v->age      = ++noteCounter;
        v->amp.start();
        v->filter.level = 0.f;
        v->filter.start();
    }

    void BasicSynthEngine::noteOff( int Note )
    {
        for( auto& v : voices )
            if( v.note == Note && v.gate )
            {
                v.gate = false;
                v.amp.release();
                v.filter.release();
            }
    }

    void BasicSynthEngine::allNotesOff()
    {
        for( auto& v : voices )
        {
            v.gate = false;
            v.amp.release();
            v.filter.release();
        }
    }

    void BasicSynthEngine::process( float* const* Out, int NumChannels, int NumSamples )
    {
        for( int ch = 0; ch < NumChannels; ++ch )
            juce::FloatVectorOperations::clear( Out[ch], NumSamples );
        if( NumChannels <= 0 )
            return;

        const auto sr        = (float)sampleRate;
        const auto wave      = values[kWave];
        const auto detune    = values[kDetune];
        const auto subLevel  = values[kSub];
        const auto k         = 2.f * ( 1.f - 0.97f * values[kResonance] ); // SVF damping
        const auto envOct    = 6.f * values[kEnvAmount];                   // up to +6 octaves
        const auto attackInc = 1.f / juce::jmax( 1.f, values[kAttack] * sr );
        const auto decayC    = decayCoef( values[kDecay], sampleRate );
        const auto sustain   = values[kSustain];
        const auto releaseC  = decayCoef( values[kRelease], sampleRate );
        const auto fDecayC   = decayCoef( values[kFilterDecay], sampleRate );
        const auto level     = values[kLevel] * 0.25f; // headroom for 8 voices

        float* left  = Out[0];
        float* right = NumChannels > 1 ? Out[1] : nullptr;

        for( int i = 0; i < NumSamples; ++i )
        {
            const auto baseCutoff = smoothCutoff.getNextValue();
            float      mix        = 0.f;

            for( auto& v : voices )
            {
                if( !v.amp.active() )
                    continue;

                const double f1  = noteToHz( v.note - detune * 0.005 );
                const double f2  = noteToHz( v.note + detune * 0.005 );
                const double dt1 = f1 / sampleRate, dt2 = f2 / sampleRate, dtSub = 0.5 * f1 / sampleRate;

                const float o1  = juce::jmap( wave, sawBlep( v.phase1, dt1 ), squareBlep( v.phase1, dt1 ) );
                const float o2  = juce::jmap( wave, sawBlep( v.phase2, dt2 ), squareBlep( v.phase2, dt2 ) );
                const float sub = (float)std::sin( juce::MathConstants<double>::twoPi * v.phaseSub );
                v.phase1        = std::fmod( v.phase1 + dt1, 1.0 );
                v.phase2        = std::fmod( v.phase2 + dt2, 1.0 );
                v.phaseSub      = std::fmod( v.phaseSub + dtSub, 1.0 );

                const float x = 0.5f * ( o1 + o2 ) + subLevel * sub;

                // Filter envelope: instant attack, decays to 0 (no sustain), scaled by velocity.
                const float fEnv   = v.filter.next( 1.f, fDecayC, 0.f, fDecayC );
                const float cutoff = juce::jlimit( 20.f, 0.45f * sr, baseCutoff * std::exp2( envOct * fEnv * ( 0.5f + 0.5f * v.velocity ) ) );

                // TPT state-variable low-pass.
                const float g  = std::tan( juce::MathConstants<float>::pi * cutoff / sr );
                const float a1 = 1.f / ( 1.f + g * ( g + k ) );
                const float v3 = x - v.ic2;
                const float v1 = a1 * ( v.ic1 + g * v3 );
                const float v2 = v.ic2 + g * v1;
                v.ic1          = 2.f * v1 - v.ic1;
                v.ic2          = 2.f * v2 - v.ic2;

                const float amp = v.amp.next( attackInc, decayC, sustain, releaseC );
                mix += v2 * amp * ( 0.3f + 0.7f * v.velocity );
            }

            mix *= level;
            left[i] = mix;
            if( right )
                right[i] = mix;
        }
    }

    //==============================================================================
    namespace
    {
        std::map<juce::String, InstrumentEngineRegistry::Factory>& registry()
        {
            static std::map<juce::String, InstrumentEngineRegistry::Factory> engines = {
                { "Basic Synth", []() { return std::make_unique<BasicSynthEngine>(); } },
            };
            return engines;
        }
    } // namespace

    void InstrumentEngineRegistry::add( const juce::String& Name, Factory F ) { registry()[Name] = std::move( F ); }

    std::unique_ptr<IInstrumentEngine> InstrumentEngineRegistry::create( const juce::String& Name )
    {
        auto& r  = registry();
        auto  it = r.find( Name );
        return it != r.end() ? it->second() : nullptr;
    }

    juce::StringArray InstrumentEngineRegistry::getNames()
    {
        juce::StringArray names;
        for( auto& [name, factory] : registry() )
            names.add( name );
        return names;
    }

    juce::String InstrumentEngineRegistry::getDefault() { return "Basic Synth"; }
} // namespace mt
