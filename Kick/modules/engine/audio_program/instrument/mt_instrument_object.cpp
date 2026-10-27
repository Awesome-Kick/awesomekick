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
    void InstrumentObject::allNotesOff() { push( { Event::Type::AllNotesOff, 0, 0.f } ); }
    void InstrumentObject::setParam( int Index, float Value ) { push( { Event::Type::Param, Index, Value } ); }

    void InstrumentObject::prepareToPlay( uint32_t SampleRate, uint32_t BlockSize )
    {
        AudIO::Object::prepareToPlay( SampleRate, BlockSize );
        AudIO::DawInserts::prepareToPlay( SampleRate, BlockSize );

        const juce::SpinLock::ScopedLockType lock( engineLock );
        if( engine )
            engine->prepare( SampleRate, (int)BlockSize );
        prepared = true;
    }

    void InstrumentObject::applyEvents()
    {
        const auto scope = fifo.read( fifo.getNumReady() );
        auto       apply = [this]( const Event& e ) {
            switch( e.type )
            {
                case Event::Type::NoteOn:
                {
                    const auto note = e.index + master.semi();
                    if( juce::isPositiveAndBelow( note, 128 ) )
                        engine->noteOn( note, e.value );
                    break;
                }
                case Event::Type::NoteOff:
                {
                    const auto note = e.index + master.semi();
                    if( juce::isPositiveAndBelow( note, 128 ) )
                        engine->noteOff( note );
                    break;
                }
                case Event::Type::AllNotesOff: engine->allNotesOff(); break;
                case Event::Type::Param: engine->setParam( e.index, e.value ); break;
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

        applyEvents();

        float* outs[2] = { outL, outR };
        engine->process( outs, 2, numSamples );

        // Program gain, pan and mute (same law as the drum voices).
        juce::FloatVectorOperations::multiply( outL, master.gainL.load(), numSamples );
        juce::FloatVectorOperations::multiply( outR, master.gainR.load(), numSamples );

        AudIO::DawInserts::process( Buffer );
        meter.process( Buffer );
    }
} // namespace mt
