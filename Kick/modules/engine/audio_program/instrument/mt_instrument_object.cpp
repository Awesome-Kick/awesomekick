/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

namespace mt
{
    InstrumentObject::InstrumentObject( AudIO::Design& D, const std::string& Name )
        : AudIO::Object( 0, 2, 0 )
        , AudIO::DawRouting( D, AudIO::BusType::Object, AudIO::BusWidth::Stereo )
        , design( D )
    {
        AudIO::Object::setName( Name );
        AudIO::DawRouting::setID( getId() );
    }

    InstrumentObject::~InstrumentObject() { design.removeMeter( getId() ); }

    void InstrumentObject::setProgram( MTIProgram* Program )
    {
        master.attachTo( Program ? Program->getState() : juce::ValueTree{ IDs::PROGRAM } );
    }

    void InstrumentObject::setEngine( std::unique_ptr<IInstrumentEngine> Engine )
    {
        if( Engine && prepared.load() )
            Engine->prepare( getSampleRate(), (int)blockSize() );

        {
            const juce::SpinLock::ScopedLockType lock( engineLock );
            std::swap( engine, Engine );
            sustainDown = false;
            sustained.fill(false);
            sounding.fill( -1 ); // the new engine has no notes
            numDeferredOffs = 0;
        }
        // `Engine` now holds the old one and is destroyed here, off the audio thread.
    }

    void InstrumentObject::push( const Event& E )
    {
        const juce::ScopedLock lock( pushLock );
        const auto             scope = fifo.write( 1 );
        if( scope.blockSize1 > 0 )
            events[(size_t)scope.startIndex1] = E;
        else if( scope.blockSize2 > 0 )
            events[(size_t)scope.startIndex2] = E;
        // Full queue: drop the event rather than block a UI or MIDI thread.
    }

    void InstrumentObject::noteOn( int Note, int Velocity )
    {
        push( { Event::Type::NoteOn, Note, juce::jlimit( 0.f, 1.f, Velocity / 127.f ) } );
    }
    void InstrumentObject::noteOff( int Note ) { push( { Event::Type::NoteOff, Note, 0.f } ); }
    void InstrumentObject::sustain(bool Down) { push({Event::Type::Sustain, 0, Down ? 1.f : 0.f}); }
    void InstrumentObject::allNotesOff() { push( { Event::Type::AllNotesOff, 0, 0.f } ); }
    void InstrumentObject::setParam( int Index, float Value ) { push( { Event::Type::Param, Index, Value } ); }
    void InstrumentObject::pitchBend( float Semitones ) { push( { Event::Type::PitchBend, 0, Semitones } ); }
    void InstrumentObject::modWheel( float Amount ) { push( { Event::Type::ModWheel, 0, Amount } ); }
    void InstrumentObject::aftertouch( int Note, int Value ) { push( { Event::Type::Aftertouch, Note, juce::jlimit( 0.f, 1.f, Value / 127.f ) } ); }
    void InstrumentObject::channelPressure( int Value ) { push( { Event::Type::ChannelPressure, 0, juce::jlimit( 0.f, 1.f, Value / 127.f ) } ); }
    void InstrumentObject::setTempo( double Bpm ) { push( { Event::Type::Tempo, 0, (float)Bpm } ); }

    void InstrumentObject::prepareToPlay( uint32_t SampleRate, uint32_t BlockSize )
    {
        AudIO::Object::prepareToPlay( SampleRate, BlockSize );
        AudIO::DawInserts::prepareToPlay( SampleRate, BlockSize );

        gainL.reset( SampleRate, 0.02 );
        gainR.reset( SampleRate, 0.02 );
        gainL.setCurrentAndTargetValue( master.gainL.load() );
        gainR.setCurrentAndTargetValue( master.gainR.load() );

        const juce::SpinLock::ScopedLockType lock( engineLock );
        if( engine )
        {
            engine->reset();
            engine->prepare( SampleRate, (int)BlockSize );
        }
        sustainDown = false;
        sustained.fill(false);
        sounding.fill( -1 );
        numDeferredOffs = 0;
        prepared        = true;
    }

    void InstrumentObject::releaseNote( int InputNote )
    {
        auto& sent = sounding[(size_t)InputNote];
        if( sent >= 0 )
            engine->noteOff( sent );
        sent = -1;
    }

    void InstrumentObject::applyEvents()
    {
        // Note-offs held back from the previous block (see below) go first.
        for( int i = 0; i < numDeferredOffs; ++i )
            releaseNote( deferredOffs[(size_t)i] );
        numDeferredOffs = 0;

        std::array<bool, 128> startedThisBlock{};

        const auto scope = fifo.read( fifo.getNumReady() );
        auto       apply = [&]( const Event& e ) {
            switch( e.type )
            {
                case Event::Type::NoteOn:
                {
                    if( !juce::isPositiveAndBelow( e.index, 128 ) )
                        break;
                    numDeferredOffs = (int)( std::remove( deferredOffs.begin(), deferredOffs.begin() + numDeferredOffs, e.index ) - deferredOffs.begin() );
                    sustained[(size_t)e.index] = false;
                    releaseNote( e.index ); // retrigger
                    // Remember the note actually sent, so the note-off matches even if the
                    // program's transpose changes while it is held.
                    const auto note = e.index + master.semi();
                    if( juce::isPositiveAndBelow( note, 128 ) )
                    {
                        engine->noteOn( note, e.value );
                        sounding[(size_t)e.index]         = note;
                        startedThisBlock[(size_t)e.index] = true;
                    }
                    break;
                }
                case Event::Type::NoteOff:
                {
                    if( !juce::isPositiveAndBelow( e.index, 128 ) )
                        break;
                    if(sustainDown) { sustained[(size_t)e.index] = true; break; }
                    // A note shorter than one block would start and stop before rendering a
                    // sample; keep it for this block and release it at the next.
                    if( startedThisBlock[(size_t)e.index] && numDeferredOffs < (int)deferredOffs.size() )
                        deferredOffs[(size_t)numDeferredOffs++] = e.index;
                    else
                        releaseNote( e.index );
                    break;
                }
                case Event::Type::Sustain:
                    sustainDown = e.value >= 0.5f;
                    if(!sustainDown)
                        for(size_t note = 0; note < sustained.size(); ++note)
                            if(sustained[note]) { releaseNote((int)note); sustained[note] = false; }
                    break;
                case Event::Type::AllNotesOff:
                    sustainDown = false;
                    sustained.fill(false);
                    engine->allNotesOff();
                    sounding.fill( -1 );
                    numDeferredOffs = 0;
                    break;
                case Event::Type::Param: engine->setParam( e.index, e.value ); break;
                case Event::Type::PitchBend: engine->pitchBend( e.value ); break;
                case Event::Type::ModWheel: engine->modWheel( e.value ); break;
                case Event::Type::Aftertouch:
                    // To the note actually sounding for that input note (see NoteOn).
                    if( juce::isPositiveAndBelow( e.index, 128 ) && sounding[(size_t)e.index] >= 0 )
                        engine->aftertouch( sounding[(size_t)e.index], e.value );
                    break;
                case Event::Type::ChannelPressure: engine->channelPressure( e.value ); break;
                case Event::Type::Tempo: engine->setTempo( e.value ); break;
            }
        };
        for( int i = 0; i < scope.blockSize1; ++i )
            apply( events[(size_t)( scope.startIndex1 + i )] );
        for( int i = 0; i < scope.blockSize2; ++i )
            apply( events[(size_t)( scope.startIndex2 + i )] );
    }

    void InstrumentObject::process( AudIO::Buffer<float>& Buffer )
    {
        const int numSamples = (int)blockSize();
        auto*     outL       = Buffer.getOutputBuffer( 0 );
        auto*     outR       = Buffer.getOutputBuffer( 1 );

        const juce::SpinLock::ScopedTryLockType lock( engineLock );
        if( !lock.isLocked() || !engine || !outL || !outR )
        {
            Buffer.clearOutputs();
            return;
        }

        // Decaying filters and envelopes otherwise end in denormals (slow on x86, and
        // engines ported from elsewhere may not guard against them).
        const juce::ScopedNoDenormals noDenormals;

        applyEvents();

        float* outs[2] = { outL, outR };
        engine->process( outs, 2, numSamples );

        // Program gain, pan and mute (same law as the drum voices), ramped to avoid zipper noise.
        gainL.setTargetValue( master.gainL.load() );
        gainR.setTargetValue( master.gainR.load() );
        gainL.applyGain( outL, numSamples );
        gainR.applyGain( outR, numSamples );

        AudIO::DawInserts::process( Buffer );
        meter.process( Buffer );
    }
} // namespace mt
