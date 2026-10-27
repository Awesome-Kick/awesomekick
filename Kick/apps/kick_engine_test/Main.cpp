/*
 * KickEngineTest: headless checks for the Kick audio engine on a desktop.
 *
 *  1. Unit tests for the reconstructed AudIO_juce objects (SampleRecorder,
 *     Looper, Filter, ADSR), fed with synthetic audio.
 *  2. An end-to-end run through mt::getAudioInterface(): load a drum kit onto
 *     pads, hit the pads the way the hardware does, and measure what reaches
 *     the audio device; then drive the sampler and looper through the API.
 *
 * Results go to engine_test_results.txt next to the executable. The exit code
 * is the number of failed checks. Pass a folder of WAVs as the first argument
 * to choose the kit (default: samples/Test Kit next to the executable).
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#include "audio_engine/audio_engine.h"

#if JUCE_WINDOWS
    #include <windows.h>
    #include <dbghelp.h>
    #pragma comment( lib, "dbghelp.lib" )

// Writes a symbolised stack trace next to the executable if the process crashes.
static LONG WINAPI writeCrashTrace( EXCEPTION_POINTERS* Info )
{
    auto process = GetCurrentProcess();
    SymSetOptions( SYMOPT_LOAD_LINES | SYMOPT_UNDNAME );
    SymInitialize( process, nullptr, TRUE );

    CONTEXT      ctx   = *Info->ContextRecord;
    STACKFRAME64 frame = {};
    frame.AddrPC.Offset    = ctx.Rip;
    frame.AddrFrame.Offset = ctx.Rbp;
    frame.AddrStack.Offset = ctx.Rsp;
    frame.AddrPC.Mode = frame.AddrFrame.Mode = frame.AddrStack.Mode = AddrModeFlat;

    juce::String trace;
    trace << "exception 0x" << juce::String::toHexString( (int)Info->ExceptionRecord->ExceptionCode ) << "\n";
    for( int i = 0; i < 40 && StackWalk64( IMAGE_FILE_MACHINE_AMD64, process, GetCurrentThread(), &frame, &ctx, nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr ); ++i )
    {
        char         buf[sizeof( SYMBOL_INFO ) + 512] = {};
        auto*        sym = (SYMBOL_INFO*)buf;
        sym->SizeOfStruct = sizeof( SYMBOL_INFO );
        sym->MaxNameLen   = 511;
        DWORD64         disp = 0;
        DWORD           lineDisp = 0;
        IMAGEHLP_LINE64 line = { sizeof( IMAGEHLP_LINE64 ) };
        trace << "  " << ( SymFromAddr( process, frame.AddrPC.Offset, &disp, sym ) ? juce::String( sym->Name ) : juce::String( "?" ) );
        if( SymGetLineFromAddr64( process, frame.AddrPC.Offset, &lineDisp, &line ) )
            trace << "  " << juce::File( line.FileName ).getFileName() << ":" << (int)line.LineNumber;
        trace << "\n";
    }
    juce::File::getSpecialLocation( juce::File::currentExecutableFile ).getSiblingFile( "engine_test_crash.txt" ).replaceWithText( trace );
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

namespace
{
    constexpr uint32_t testRate  = 48000;
    constexpr uint32_t testBlock = 128;

    struct Results
    {
        juce::StringArray lines;
        int               failed{ 0 }, passed{ 0 };

        void section( const juce::String& Name ) { lines.add( "\n== " + Name ); }
        void note( const juce::String& Text ) { lines.add( "   " + Text ); }
        bool check( bool Ok, const juce::String& Name, const juce::String& Detail = {} )
        {
            ( Ok ? passed : failed )++;
            lines.add( juce::String( Ok ? "PASS " : "FAIL " ) + Name + ( Detail.isNotEmpty() ? "  [" + Detail + "]" : juce::String() ) );
            return Ok;
        }
    };

    void pump( int Ms ) { juce::MessageManager::getInstance()->runDispatchLoopUntil( Ms ); }

    float rms( const float* d, int n )
    {
        double s = 0;
        for( int i = 0; i < n; ++i )
            s += (double)d[i] * d[i];
        return n > 0 ? (float)std::sqrt( s / n ) : 0.f;
    }

    juce::String f3( double v ) { return juce::String( v, 3 ); }

    // Feeds a sine into a two-channel AudIO buffer, one block at a time.
    struct SineSource
    {
        // 375 Hz is exactly one cycle per 128-sample block, so every loop pass
        // lines up in phase and overdubs sum predictably.
        double phase{ 0 }, freq{ 375 };
        float  ampL{ 0.5f }, ampR{ 0.25f };

        void fill( AudIO::Buffer<float>& B, bool Silent = false )
        {
            auto* l = B.getInputBuffer( 0 );
            auto* r = B.getInputBuffer( 1 );
            for( uint32_t i = 0; i < testBlock; ++i )
            {
                const auto s = (float)std::sin( phase );
                phase += juce::MathConstants<double>::twoPi * freq / testRate;
                l[i] = Silent ? 0.f : s * ampL;
                r[i] = Silent ? 0.f : s * ampR;
            }
        }
    };

    struct TestSampleRecorder : public AudIO::SampleRecorder
    {
        using SampleRecorder::SampleRecorder;
        void prep() { prepareToPlay( testRate, testBlock ); }
        void run( AudIO::Buffer<float>& B ) { process( B ); }
    };

    struct TestLooper : public AudIO::Looper
    {
        using Looper::Looper;
        void prep() { prepareToPlay( testRate, testBlock ); }
        void run( AudIO::Buffer<float>& B ) { process( B ); }
    };

    //==============================================================================
    void testSampleRecorder( Results& R )
    {
        R.section( "SampleRecorder (synthetic input)" );

        AudIO::Design        design( 2, 2 );
        TestSampleRecorder   rec( design );
        AudIO::Buffer<float> buf( 2, true, 2, true );
        SineSource           sine;
        rec.prep();

        bool   stopped = false;
        double lengthCb{ -1 };
        rec.onRecorderStopped       = [&]() { stopped = true; };
        rec.onRecorderLengthChanged = [&]( double t ) { lengthCb = t; };

        // Fill a 0.5 s buffer to the end.
        rec.setRecordingMode( AudIO::Recorder::LR_STEREO );
        rec.setThreshold( 0.f );
        rec.setGainAndPan( 1.f, 0.f );
        rec.setLength( 0.5 );
        rec.startRecording( true );
        R.check( rec.isRecording(), "starts recording" );

        int blocks = 0;
        while( rec.isRecording() && blocks++ < 400 )
        {
            sine.fill( buf );
            rec.run( buf );
        }
        pump( 50 ); // let the async stop run on the message thread

        auto& data = rec.getAudioBuffer();
        R.check( !rec.isRecording(), "stops by itself when the buffer is full", "blocks=" + juce::String( blocks ) );
        R.check( stopped, "onRecorderStopped fired" );
        R.check( data.getNumChannels() == 2 && data.getNumSamples() == (int)( 0.5 * testRate ), "buffer is 0.5 s stereo", juce::String( data.getNumChannels() ) + "ch x " + juce::String( data.getNumSamples() ) );
        R.check( std::abs( lengthCb - 0.5 ) < 0.001, "onRecorderLengthChanged reports 0.5 s", f3( lengthCb ) );

        const auto rmsL = rms( data.getReadPointer( 0 ), data.getNumSamples() );
        const auto rmsR = rms( data.getReadPointer( 1 ), data.getNumSamples() );
        R.check( std::abs( rmsL - 0.3536f ) < 0.01f && std::abs( rmsR - 0.1768f ) < 0.01f, "recorded levels match input (L 0.354, R 0.177)", "L " + f3( rmsL ) + " R " + f3( rmsR ) );
        R.check( std::abs( rec.readProgress() - 1.f ) < 0.01f, "progress is 100%", f3( rec.readProgress() ) );

        // Preview plays back what was recorded.
        rec.startPreview( 0.0 );
        sine.fill( buf, true );
        rec.run( buf );
        float maxErr = 0;
        for( uint32_t i = 0; i < testBlock; ++i )
            maxErr = juce::jmax( maxErr, std::abs( buf.getOutputBuffer( 0 )[i] - data.getSample( 0, (int)i ) ) );
        R.check( rec.isPreviewing() && maxErr < 1.0e-6f, "preview outputs the recording", "max error " + juce::String( maxErr ) );
        rec.stopPreview();

        // Saving writes a readable WAV of the same length.
        auto wavFile = juce::File::getSpecialLocation( juce::File::tempDirectory ).getChildFile( "kick_recorder_test.wav" );
        rec.saveRecording( wavFile.getFullPathName().toStdString() );
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader( fm.createReaderFor( wavFile ) );
        R.check( reader && reader->lengthInSamples == data.getNumSamples() && reader->numChannels == 2, "saveRecording writes a matching WAV", reader ? juce::String( reader->lengthInSamples ) + " samples" : "unreadable" );
        reader.reset();
        wavFile.deleteFile();

        // Stopping early trims to what was recorded.
        rec.reset();
        rec.setLength( 1.0 );
        rec.startRecording( true );
        for( int b = 0; b < 100; ++b )
        {
            sine.fill( buf );
            rec.run( buf );
        }
        rec.stopRecording();
        R.check( data.getNumSamples() == 100 * (int)testBlock, "stopping early trims the buffer", juce::String( data.getNumSamples() ) + " samples" );

        // Mono modes record one channel; L_MONO takes the left input.
        rec.reset();
        rec.setRecordingMode( AudIO::Recorder::L_MONO );
        rec.setLength( 0.1 );
        rec.startRecording( true );
        while( rec.isRecording() )
        {
            sine.fill( buf );
            rec.run( buf );
        }
        pump( 20 );
        R.check( data.getNumChannels() == 1 && std::abs( rms( data.getReadPointer( 0 ), data.getNumSamples() ) - 0.3536f ) < 0.01f, "L_MONO records the left input into one channel", juce::String( data.getNumChannels() ) + "ch rms " + f3( rms( data.getReadPointer( 0 ), data.getNumSamples() ) ) );

        // Threshold holds recording back until the input is loud enough.
        rec.reset();
        rec.setRecordingMode( AudIO::Recorder::LR_STEREO );
        rec.setThreshold( 0.3f );
        rec.setLength( 0.5 );
        rec.startRecording( true );
        for( int b = 0; b < 20; ++b )
        {
            sine.fill( buf, true );
            rec.run( buf );
        }
        const auto progressQuiet = rec.readProgress();
        for( int b = 0; b < 20; ++b )
        {
            sine.fill( buf );
            rec.run( buf );
        }
        R.check( progressQuiet == 0.f && rec.readProgress() > 0.f, "threshold waits for loud input", "quiet " + f3( progressQuiet ) + " loud " + f3( rec.readProgress() ) );
        rec.stopRecording();
        rec.setThreshold( 0.f );

        // Reverse recording fills the buffer from the end.
        rec.reset();
        rec.setDirection( AudIO::Recorder::REVERSE );
        rec.setLength( 0.05 );
        rec.startRecording( true );
        float ramp = 0;
        while( rec.isRecording() )
        {
            for( uint32_t i = 0; i < testBlock; ++i, ramp += 1.f / testRate )
                buf.getInputBuffer( 0 )[i] = buf.getInputBuffer( 1 )[i] = ramp;
            rec.run( buf );
        }
        pump( 20 );
        R.check( data.getNumSamples() > 0 && data.getSample( 0, 0 ) > data.getSample( 0, data.getNumSamples() - 1 ), "reverse recording runs backwards", "first " + f3( data.getNumSamples() ? data.getSample( 0, 0 ) : 0 ) + " last " + f3( data.getNumSamples() ? data.getSample( 0, data.getNumSamples() - 1 ) : 0 ) );
        rec.setDirection( AudIO::Recorder::FORWARD );
    }

    //==============================================================================
    void testLooper( Results& R )
    {
        R.section( "Looper (synthetic input)" );

        AudIO::Design        design( 2, 2 );
        TestLooper           lp( design );
        AudIO::Buffer<float> buf( 2, true, 2, true );
        SineSource           sine;
        sine.ampL = sine.ampR = 0.5f;
        lp.prep();

        const int loopBlocks = 100;
        lp.setLength( loopBlocks * testBlock / (double)testRate );

        // Plays one loop with silent input; returns output RMS for L and R.
        auto playLoop = [&]() {
            std::vector<float> l, r;
            for( int b = 0; b < loopBlocks; ++b )
            {
                sine.fill( buf, true );
                lp.run( buf );
                l.insert( l.end(), buf.getOutputBuffer( 0 ), buf.getOutputBuffer( 0 ) + testBlock );
                r.insert( r.end(), buf.getOutputBuffer( 1 ), buf.getOutputBuffer( 1 ) + testBlock );
            }
            return std::make_pair( rms( l.data(), (int)l.size() ), rms( r.data(), (int)r.size() ) );
        };
        auto recordLoop = [&]() {
            for( int b = 0; b < loopBlocks; ++b )
            {
                sine.fill( buf );
                lp.run( buf );
            }
        };

        R.check( lp.isEmpty(), "starts empty" );
        lp.setRecord( true );
        lp.start( true, true );
        R.check( lp.isRunning() && lp.isRecording(), "record starts the loop" );
        recordLoop();
        R.check( !lp.isEmpty(), "has content after one pass" );

        lp.setPlay();
        R.check( lp.isPreviewing() && !lp.isRecording(), "setPlay switches to playback" );
        auto [l1, r1] = playLoop();
        R.check( std::abs( l1 - 0.3536f ) < 0.01f && std::abs( r1 - 0.3536f ) < 0.01f, "plays back what was recorded", "L " + f3( l1 ) + " R " + f3( r1 ) );

        lp.setOverdub( true );
        lp.start( false, true );
        recordLoop();
        lp.setPlay();
        auto [l2, r2] = playLoop();
        R.check( std::abs( l2 - 0.7071f ) < 0.02f, "overdub layers on top (same-phase sine doubles)", "L " + f3( l2 ) );

        lp.undo();
        auto [l3, r3] = playLoop();
        R.check( std::abs( l3 - 0.3536f ) < 0.01f, "undo removes the overdub", "L " + f3( l3 ) );
        lp.redo();
        auto [l4, r4] = playLoop();
        R.check( std::abs( l4 - 0.7071f ) < 0.02f, "redo restores it", "L " + f3( l4 ) );

        lp.setPan( 1.f );
        auto [l5, r5] = playLoop();
        R.check( l5 < 0.001f && std::abs( r5 - 0.7071f ) < 0.02f, "pan hard right silences the left", "L " + f3( l5 ) + " R " + f3( r5 ) );
        lp.setPan( 0.f );
        lp.setGain( 0.5f );
        auto [l6, r6] = playLoop();
        R.check( std::abs( l6 - 0.3536f ) < 0.01f, "playback gain applies", "L " + f3( l6 ) );
        lp.setGain( 1.f );

        lp.stop();
        auto [l7, r7] = playLoop();
        R.check( !lp.isRunning() && l7 < 0.001f, "stop silences the loop", "L " + f3( l7 ) );

        lp.clear();
        R.check( lp.isEmpty(), "clear empties it" );

        lp.setThreshold( 0.3f );
        lp.setRecord( true );
        lp.start( true, false );
        for( int b = 0; b < loopBlocks; ++b )
        {
            sine.fill( buf, true );
            lp.run( buf );
        }
        const bool emptyAfterQuiet = lp.isEmpty();
        recordLoop();
        R.check( emptyAfterQuiet && !lp.isEmpty(), "threshold waits for loud input" );
        lp.stop();
        lp.setThreshold( 0.f );

        lp.setMonitorOn( true );
        lp.setMonitorGain( 0.5f );
        sine.fill( buf );
        lp.run( buf );
        const auto mon = rms( buf.getOutputBuffer( 0 ), testBlock );
        R.check( std::abs( mon - 0.177f ) < 0.03f, "monitor passes input at monitor gain", f3( mon ) );
    }

    //==============================================================================
    float filterGain( mt::FilterType Type, float CutoffHz, float SignalHz, double Resonance = 0 )
    {
        mt::Filter f;
        f.setSampleRate( testRate );
        f.setFilterType( Type );
        f.setResonance( Resonance );
        f.setCutoff( mt::Filter::freqRange.convertTo0to1( CutoffHz ) );

        juce::AudioBuffer<float> frame( 2, 1 );
        double                   in2 = 0, out2 = 0;
        for( int i = 0; i < (int)testRate; ++i )
        {
            const auto x = (float)std::sin( juce::MathConstants<double>::twoPi * SignalHz * i / testRate );
            frame.setSample( 0, 0, x );
            frame.setSample( 1, 0, x );
            f.process( frame );
            if( i > (int)testRate / 4 ) // skip the settling time
            {
                in2 += x * x;
                out2 += frame.getSample( 0, 0 ) * frame.getSample( 0, 0 );
            }
        }
        return (float)std::sqrt( out2 / in2 );
    }

    void testFilter( Results& R )
    {
        R.section( "Voice filter" );
        using T = mt::FilterType;

        const auto lpStop = filterGain( T::LowPass4, 500, 5000 ), lpPass = filterGain( T::LowPass4, 500, 100 );
        R.check( lpStop < 0.01f && lpPass > 0.9f, "Lowpass 4 at 500 Hz: passes 100 Hz, cuts 5 kHz", "100Hz " + f3( lpPass ) + " 5kHz " + f3( lpStop ) );

        const auto hpStop = filterGain( T::HighPass2, 2000, 100 ), hpPass = filterGain( T::HighPass2, 2000, 10000 );
        R.check( hpStop < 0.01f && hpPass > 0.9f, "Highpass 2 at 2 kHz: cuts 100 Hz, passes 10 kHz", "100Hz " + f3( hpStop ) + " 10kHz " + f3( hpPass ) );

        const auto bpCentre = filterGain( T::BandPass2, 1000, 1000 ), bpOff = filterGain( T::BandPass2, 1000, 100 );
        R.check( bpCentre > 0.9f && bpOff < 0.2f, "Bandpass 2 at 1 kHz", "1kHz " + f3( bpCentre ) + " 100Hz " + f3( bpOff ) );

        const auto notch = filterGain( T::BandStop2, 1000, 1000 );
        R.check( notch < 0.05f, "Bandstop 2 notches its centre", f3( notch ) );

        const auto reso = filterGain( T::LowPass2, 1000, 1000, 90 );
        R.check( reso > 2.f, "resonance boosts the cutoff frequency", f3( reso ) );

        const auto lp1 = filterGain( T::LowPass1, 1000, 1000 );
        R.check( std::abs( lp1 - 0.707f ) < 0.03f, "Lowpass 1 is -3 dB at cutoff", f3( lp1 ) );

        // Every type stays finite with full resonance on noise.
        juce::Random rnd( 1 );
        bool         allFinite = true;
        juce::String bad;
        for( int t = 0; t <= (int)T::Kick3000LPF; ++t )
        {
            mt::Filter f;
            f.setSampleRate( testRate );
            f.setFilterType( (T)t );
            f.setResonance( 100 );
            juce::AudioBuffer<float> frame( 2, 1 );
            for( int i = 0; i < (int)testRate; ++i )
            {
                f.setCutoff( 0.5 + 0.5 * std::sin( i * 0.001 ) ); // sweep, as modulation does
                frame.setSample( 0, 0, rnd.nextFloat() * 2 - 1 );
                frame.setSample( 1, 0, rnd.nextFloat() * 2 - 1 );
                f.process( frame );
                if( !std::isfinite( frame.getSample( 0, 0 ) ) || std::abs( frame.getSample( 0, 0 ) ) > 100.f )
                {
                    allFinite = false;
                    bad << t << " ";
                    break;
                }
            }
        }
        R.check( allFinite, "all 27 types stay stable at full resonance with a swept cutoff", bad );
    }

    void testADSR( Results& R )
    {
        R.section( "Voice envelope" );
        mt::ADSR env;
        env.setParameters( 10, 5, 10, 10, 0.5 );
        env.gate( 1 );

        std::vector<float> v;
        for( int i = 0; i < 40; ++i )
            v.push_back( env() );
        R.check( std::abs( v[4] - 0.5f ) < 0.01f && v[9] == 1.f, "attack ramps to 1 over 10 samples", f3( v[4] ) + " " + f3( v[9] ) );
        R.check( v[12] == 1.f, "hold stays at 1" );
        R.check( std::abs( v[39] - 0.5f ) < 0.001f, "decays to sustain", f3( v[39] ) );

        env.gate( 0 );
        float last = 1;
        for( int i = 0; i < 10; ++i )
            last = env();
        R.check( last == 0.f && !env.isActive(), "release reaches 0 and goes idle", f3( last ) );

        mt::ADSR instant;
        instant.setParameters( 0, 0, 0, 0, 1 );
        instant.gate( 1 );
        R.check( instant() == 1.f, "zero attack is instant (pitch envelope default)" );
    }

    //==============================================================================
    // Peak device output level over a window, in linear gain.
    float measureOutput( juce::AudioDeviceManager::LevelMeter::Ptr Meter, int Ms )
    {
        float peak = 0;
        auto  end  = juce::Time::getMillisecondCounter() + (juce::uint32)Ms;
        while( juce::Time::getMillisecondCounter() < end )
        {
            pump( 5 );
            peak = juce::jmax( peak, (float)Meter->getCurrentLevel() );
        }
        return peak;
    }

    void testEngine( Results& R, const juce::File& KitDir )
    {
        R.section( "Engine end to end (real audio device)" );

        auto& ai = mt::getAudioInterface();
        pump( 1500 ); // the device is opened asynchronously

        auto& dm  = ai.getAudioDeviceManager();
        auto* dev = dm.getCurrentAudioDevice();
        if( !R.check( dev != nullptr, "audio device opened" ) )
            return;

        R.note( "device: " + dm.getCurrentAudioDeviceType() + " / " + dev->getName() + " @ " + juce::String( dev->getCurrentSampleRate() ) + " Hz, " + juce::String( dev->getCurrentBufferSizeSamples() ) + " samples" );
        R.note( "inputs active: " + juce::String( dev->getActiveInputChannels().countNumberOfSetBits() ) + ", outputs active: " + juce::String( dev->getActiveOutputChannels().countNumberOfSetBits() ) );

        auto meter = dm.getOutputLevelGetter();
        R.check( measureOutput( meter, 300 ) < 0.001f, "silent at idle" );

        // Build a sequence -> track -> drum program with the kit on pads 0..N.
        const auto seq  = ai.createSequence();
        const auto prog = ai.createProgram( mt::MTProgramType::Drum, "TestKit" );
        const auto trk  = ai.createTrack( seq );
        R.check( seq >= 0 && prog >= 0 && trk >= 0, "created sequence, drum program, track", "seq " + juce::String( seq ) + " prog " + juce::String( prog ) + " track " + juce::String( trk ) );
        R.check( ai.setProgramToTrack( prog, seq, trk ) == 0, "program assigned to track" );
        ai.setSelectedSequenceAndTrack( seq, trk );
        R.note( "program index by name: " + juce::String( ai.getProgramIndexByName( "TestKit" ) ) + ", selected program " + juce::String( ai.getSelectedProgram() ) );

        auto files = KitDir.findChildFiles( juce::File::findFiles, false, "*.wav" );
        files.sort();
        if( !R.check( !files.isEmpty(), "found kit samples", KitDir.getFullPathName() ) )
            return;

        // Same order as the UI: import into the project's assets, then assign.
        for( int pad = 0; pad < files.size(); ++pad )
        {
            ai.addAsset( files[pad].getFullPathName(), mt::AssetTypes::Samples );
            ai.addSampleToProgram( prog, pad, files[pad].getFullPathName() );
        }
        pump( 1000 ); // samples load in the background
        R.check( ai.getNumProgramSamples( prog ) == files.size(), "samples assigned to pads", juce::String( ai.getNumProgramSamples( prog ) ) + "/" + juce::String( files.size() ) );

        // Hit each pad exactly as AppMainmode does for a hardware pad press.
        for( int pad = 0; pad < files.size(); ++pad )
        {
            ai.addLiveMidiMsg( ai.getSelected(), juce::MidiMessage::noteOn( 1, pad, (juce::uint8)127 ) );
            const auto peak = measureOutput( meter, 350 );
            float      trackPeak{ -100 };
            for( auto& m : ai.getTrackMeter( seq, trk ) )
                trackPeak = juce::jmax( trackPeak, m.peak );
            ai.addLiveMidiMsg( ai.getSelected(), juce::MidiMessage::noteOff( 1, pad ) );
            R.check( peak > 0.05f, "pad " + juce::String( pad + 1 ) + " (" + files[pad].getFileNameWithoutExtension() + ") makes sound", "device peak " + f3( peak ) + ", track meter " + f3( trackPeak ) + " dB" );
            measureOutput( meter, 250 );
        }
        // Pads are one-shot: the last test sample is a 1.5 s tone that rings past note-off.
        measureOutput( meter, 1500 ); // let the tone finish
        const auto tail = measureOutput( meter, 1000 );
        R.check( tail < 0.01f, "silent again after the hits", "peak " + f3( tail ) );

        // Same hit, straight into the program (skips the track).
        ai.addLiveMidiNote( prog, 2, 127 );
        const auto direct = measureOutput( meter, 350 );
        ai.stopLiveMidiNote( prog, 2 );
        R.check( direct > 0.05f, "note sent straight to the program makes sound", "peak " + f3( direct ) );
        measureOutput( meter, 500 );

        // Velocity: a soft hit should be quieter than a hard one.
        ai.addLiveMidiMsg( ai.getSelected(), juce::MidiMessage::noteOn( 1, 0, (juce::uint8)30 ) );
        const auto soft = measureOutput( meter, 350 );
        measureOutput( meter, 500 );
        ai.addLiveMidiMsg( ai.getSelected(), juce::MidiMessage::noteOn( 1, 0, (juce::uint8)127 ) );
        const auto hard = measureOutput( meter, 350 );
        R.check( soft < hard * 0.8f, "velocity affects level", "v30 " + f3( soft ) + " v127 " + f3( hard ) );
        measureOutput( meter, 500 );

        // Sequencer: two notes in the track, then play the sequence.
        ai.addOrModifyNotesInTrack( seq, trk, 0, 0, 96 );
        ai.addOrModifyNotesInTrack( seq, trk, 1, 384, 96 );
        ai.setSequencePlay( seq, true, true );
        const auto seqPeak = measureOutput( meter, 2500 );
        ai.setSequenceStop();
        R.check( seqPeak > 0.05f, "sequencer plays programmed notes", "peak " + f3( seqPeak ) );
        measureOutput( meter, 500 );

        //--------------------------------------------------------------------------
        R.section( "Sampler through the engine (records the device input)" );
        bool started = false, stopped = false;
        double length = -1;
        mt::MTRecorderListener listener;
        listener.onRecorderStarted       = [&]() { started = true; };
        listener.onRecorderStopped       = [&]() { stopped = true; };
        listener.onRecorderLengthChanged = [&]( double t ) { length = t; };
        ai.setRecorderListener( listener );

        ai.enableRecorder( true );
        ai.setRecorderThreshold( 0.f );
        ai.setupRecorder( 1.0, 1.f, 0.f );
        ai.startRecorder( (int)mt::Recorder::LR_STEREO, true, true );
        pump( 1400 );

        auto& recBuf  = ai.getRecorderAudioBuffer();
        const auto sr = dev->getCurrentSampleRate();
        R.check( started && stopped, "start and stop callbacks fired", juce::String( "started " ) + ( started ? "yes" : "no" ) + ", stopped " + ( stopped ? "yes" : "no" ) );
        R.check( std::abs( recBuf.getNumSamples() - sr ) < 2 * testBlock, "recorded 1 s at the device rate", juce::String( recBuf.getNumSamples() ) + " samples @ " + juce::String( sr ) );
        R.check( std::abs( length - 1.0 ) < 0.02, "reported length is 1 s", f3( length ) );
        R.note( "input level while recording (room/mic): rms " + f3( recBuf.getNumSamples() ? rms( recBuf.getReadPointer( 0 ), recBuf.getNumSamples() ) : 0 ) );

        auto saved = juce::File::getSpecialLocation( juce::File::tempDirectory ).getChildFile( "kick_engine_sampler_test.wav" );
        ai.saveRecording( saved.getFullPathName().toStdString() );
        R.check( saved.existsAsFile() && saved.getSize() > 1000, "saveRecording wrote a file", juce::String( saved.getSize() ) + " bytes" );
        saved.deleteFile();

        //--------------------------------------------------------------------------
        R.section( "Looper through the engine" );
        ai.setLooperEnable( true );
        ai.setLooperThreshold( 0.f );
        ai.setLooperLength( 1.0 );
        ai.setLooperRecord( true );
        ai.looperStart( true, true );
        pump( 300 );
        R.check( ai.isLooperRunning() && ai.isLooperRecording(), "record starts the loop" );
        pump( 1000 );
        R.check( !ai.isLooperEmpty(), "loop has content after one pass" );
        R.check( ai.getLooperProgress() >= 0.0 && ai.getLooperProgress() < 1.0, "progress stays within the loop", f3( ai.getLooperProgress() ) );
        ai.setLooperPlay();
        pump( 200 );
        R.check( ai.isLooperPlay() && !ai.isLooperRecording(), "switches to playback" );
        ai.looperStop();
        ai.looperClear();
        R.check( !ai.isLooperRunning() && ai.isLooperEmpty(), "stop and clear" );
    }
} // namespace

//==============================================================================
class KickEngineTestApp : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "KickEngineTest"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }

    void initialise( const juce::String& CommandLine ) override
    {
#if JUCE_WINDOWS
        SetUnhandledExceptionFilter( writeCrashTrace );
    #ifdef _DEBUG
        // Report CRT asserts to the debugger output instead of blocking on a dialog.
        for( int type : { _CRT_WARN, _CRT_ERROR, _CRT_ASSERT } )
            _CrtSetReportMode( type, _CRTDBG_MODE_DEBUG );
    #endif
#endif
        juce::MessageManager::callAsync( [this, CommandLine]() { run( CommandLine.unquoted() ); } );
    }

    void shutdown() override { mt::getAudioInterface().shutdown(); }

private:
    void run( const juce::String& KitArg )
    {
        auto exeDir = juce::File::getSpecialLocation( juce::File::currentExecutableFile ).getParentDirectory();
        auto kitDir = KitArg.isNotEmpty() ? juce::File( KitArg ) : exeDir.getChildFile( "samples" ).getChildFile( "Test Kit" );

        Results R;

        // The engine owns the AudIO memory pool, so create it before any buffers.
        mt::getAudioInterface();

        testSampleRecorder( R );
        testLooper( R );
        testFilter( R );
        testADSR( R );
        testEngine( R, kitDir );

        R.lines.add( "\n" + juce::String( R.passed ) + " passed, " + juce::String( R.failed ) + " failed" );
        exeDir.getChildFile( "engine_test_results.txt" ).replaceWithText( R.lines.joinIntoString( "\n" ) );

        setApplicationReturnValue( R.failed );
        quit();
    }
};

START_JUCE_APPLICATION( KickEngineTestApp )
