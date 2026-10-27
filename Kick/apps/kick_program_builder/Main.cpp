/*
 * KickProgramBuilder: builds ready-to-play drum projects from a sample
 * library through mt::getAudioInterface(), the same calls the UI makes.
 *
 * For each kit and each output "projects" folder it:
 *   1. creates <projects>/<Kit>/ (with Samples/ and Export/, like a new project
 *      from the UI) and copies the kit's one-shots into <Kit>/Samples/,
 *   2. loads it as a new project, creates a drum program with the samples on
 *      bank A (pads 0..15), mute groups, a sequence + track with the program
 *      and a 2-bar beat, then saves and closes it,
 *   3. in a fresh child process (--verify-only), reloads each saved project and
 *      checks the program, the track notes and that every pad and the
 *      sequence actually make sound on the device.
 *
 * Usage: KickProgramBuilder [--reference] [--verify-only] <projectsDir>...
 *   default projectsDir: "projects" next to the executable.
 *   --reference   keep the samples where they are (absolute paths into the
 *                 library) instead of copying them into the project.
 *   --verify-only skip building, only reload and check the kits.
 * Results go to program_builder_results.txt next to the executable. The exit
 * code is the number of failed checks.
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#include "audio_engine/audio_engine.h"

namespace
{
    //==============================================================================
    // Kits
    //==============================================================================
    const juce::String lib = "D:\\Samples\\";

    struct Pad
    {
        int          pad;
        juce::String file;      // relative to lib
        juce::String label;
        int          muteGroup{ -1 };
    };

    struct Lane // one pad's steps in one bar; 16 x 16ths. X=127 x=100 o=60 .=rest
    {
        int          pad;
        juce::String steps;
    };

    struct Kit
    {
        juce::String      name;
        int               bpm;
        std::vector<Pad>  pads;
        std::vector<Lane> beat;
    };

    std::vector<Kit> kits()
    {
        const juce::String rsD  = "Rhythm Science_Vintage Soul Drums Vol. 3\\one_shot\\drums\\Vintage-Soul-Drums-Vol-3_Rhythm-Science_";
        const juce::String rsP  = "Rhythm Science_Vintage Soul Drums Vol. 3\\one_shot\\percussion\\Vintage-Soul-Drums-Vol-3_Rhythm-Science_";
        const juce::String flS  = "Flamingosis Inspired Drum Kit\\Snares\\";
        const juce::String flC  = "Flamingosis Inspired Drum Kit\\Claps_Snaps\\";
        const juce::String otH  = "Cr2_Oriental_Trap_WAV\\Audio_MIDI\\Drum Hits\\Hats\\";
        const juce::String esw  = "Echo_Sound_Works_Drums_That_Sauce_Volume_1\\Files\\_(One-Shots)\\";
        const juce::String e808 = "Echo_Sound_Works_Drums_That_Sauce_Volume_1\\Files\\_Filthy_808s\\";
        const juce::String lun  = "HIP_HOP_LUNAR_CYCLES\\LUN_SOUNDS_&_FX\\";

        std::vector<Kit> k;

        k.push_back( { "Vintage Soul Kit",
                       92,
                       {
                           { 0, rsD + "Kick_oneshot_gift.wav", "Kick (gift)" },
                           { 1, rsD + "Kick_oneshot_knock.wav", "Kick (knock)" },
                           { 2, lun + "LUN_DRUM_HITS\\LUN_KICKS\\LUN_Kick_03.wav", "Kick (Lunar 03)" },
                           { 3, lun + "LUN_DRUM_HITS\\LUN_KICKS\\LUN_Kick_14.wav", "Kick (Lunar 14, boom)" },
                           { 4, flS + "SP1200-SD-02.wav", "Snare (SP1200)" },
                           { 5, flS + "9thsnare3 1.wav", "Snare (9th)" },
                           { 6, flC + "DILLA-CL-0029.wav", "Clap (Dilla)" },
                           { 7, flC + "SP1200-Clap-11.wav", "Clap (SP1200)" },
                           { 8, otH + "OT_HiHat_6_(Closed_-_Acoustic).wav", "Closed hat (acoustic)", 1 },
                           { 9, otH + "OT_HiHat_15_(Open_-_Acoustic).wav", "Open hat (acoustic)", 1 },
                           { 10, rsP + "Shaker_oneshot_hammer.wav", "Shaker" },
                           { 11, rsP + "Tambourine_oneshot_drop.wav", "Tambourine" },
                           { 12, rsD + "Bongo_oneshot_bap.wav", "Bongo" },
                           { 13, rsP + "Conga_oneshot_slapper.wav", "Conga" },
                           { 14, rsP + "Cowbell_oneshot_design.wav", "Cowbell" },
                           { 15, "FAT-Drum-Pack-WAV\\FAT_CRASH_02.wav", "Crash" },
                       },
                       {
                           { 0, "X......x..X....." },
                           { 4, "....X.......X..." },
                           { 5, ".......o.......o" },
                           { 8, "x.x.x.x.x.x.x..." },
                           { 9, "..............x." },
                           { 11, "....x.......x..." },
                       } } );

        k.push_back( { "Trap Kit",
                       140,
                       {
                           { 0, esw + "_Kicks\\ESW Sauce - Kick 01 - Super Heavy.wav", "Kick (Super Heavy)" },
                           { 1, esw + "_Kicks\\ESW Sauce - Kick 19 - Knocker.wav", "Kick (Knocker)" },
                           { 2, e808 + "ESW Filthy 808 - - 06 - C.wav", "808 (06, C)", 2 },
                           { 3, e808 + "ESW Filthy 808 - - 04 - C.wav", "808 (04, C)", 2 },
                           { 4, esw + "_Snares\\ESW Sauce - Snare 01 - Zealous.wav", "Snare (Zealous)" },
                           { 5, esw + "_Snares\\ESW Sauce - Snare 07 - Stock X High.wav", "Snare (Stock X)" },
                           { 6, esw + "_Claps\\ESW Clap 00 - Bounds.wav", "Clap (Bounds)" },
                           { 7, esw + "_Claps\\ESW Clap 12 - Thwap.wav", "Clap (Thwap)" },
                           { 8, esw + "_Hi-Hats\\_Closed\\ESW Sauce - HH Closed 01 - Shuffle.wav", "Closed hat (Shuffle)", 1 },
                           { 9, esw + "_Hi-Hats\\_Closed\\ESW Sauce - HH Closed 21 - Affinity.wav", "Closed hat (Affinity)", 1 },
                           { 10, esw + "_Hi-Hats\\_Open\\ESW Sauce - HH Open 00 - Grimey.wav", "Open hat (Grimey)", 1 },
                           { 11, esw + "_Rims\\ESW Sauce - Rim 05 - Bop.wav", "Rim (Bop)" },
                           { 12, esw + "_Percussions\\ESW Sauce Perc - Shaker LoFi.wav", "Shaker (LoFi)" },
                           { 13, esw + "_Percussions\\ESW Sauce Perc - Tiny Tom.wav", "Tom (Tiny)" },
                           { 14, esw + "_Cymbals\\ESW Sauce  - Trap Crash 02 - Java.wav", "Crash (Java)" },
                           { 15, esw + "_Percussions\\ESW Sauce Perc - Reverse Whoosh.wav", "FX (Reverse Whoosh)" },
                       },
                       {
                           { 0, "X.....x...X....." },
                           { 2, "X.....x...X....." },
                           { 6, "........X......." },
                           { 4, "........x......." },
                           { 8, "xoxoxoxoxoxoxx.." },
                           { 10, "..............x." },
                       } } );

        const juce::String lh = lun + "LUN_DRUM_HITS\\";
        k.push_back( { "Lunar Boom Bap",
                       88,
                       {
                           { 0, lh + "LUN_KICKS\\LUN_Kick_01.wav", "Kick 01" },
                           { 1, lh + "LUN_KICKS\\LUN_Kick_05.wav", "Kick 05" },
                           { 2, lh + "LUN_KICKS\\LUN_Kick_10.wav", "Kick 10" },
                           { 3, lh + "LUN_KICKS\\LUN_Kick_19.wav", "Kick 19" },
                           { 4, lh + "LUN_SNARES\\LUN_Snare_01.wav", "Snare 01" },
                           { 5, lh + "LUN_SNARES\\LUN_Snare_05.wav", "Snare 05" },
                           { 6, lh + "LUN_SNARES\\LUN_Snare_09.wav", "Snare 09" },
                           { 7, lh + "LUN_SNARES\\LUN_Snare_16.wav", "Snare 16" },
                           { 8, lh + "LUN_HATS\\LUN_Hat_02.wav", "Hat 02 (closed)", 1 },
                           { 9, lh + "LUN_HATS\\LUN_Hat_13.wav", "Hat 13 (longest)", 1 },
                           { 10, lh + "LUN_HATS\\LUN_Hat_06.wav", "Hat 06", 1 },
                           { 11, lh + "LUN_PERCS\\LUN_Perc_07.wav", "Perc 07" },
                           { 12, lh + "LUN_PERCS\\LUN_Perc_01.wav", "Perc 01" },
                           { 13, lh + "LUN_PERCS\\LUN_Perc_11.wav", "Perc 11" },
                           { 14, lh + "LUN_PERCS\\LUN_Perc_16.wav", "Perc 16" },
                           { 15, lun + "LUN_FX_HITS\\LUN_FX_04.wav", "FX 04" },
                       },
                       {
                           { 0, "X..x....X.x....." },
                           { 4, "....X.......X..." },
                           { 8, "x.x.x.x.x.x.x.x." },
                           { 9, "...............o" },
                       } } );
        return k;
    }

    //==============================================================================
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

    void         pump( int Ms ) { juce::MessageManager::getInstance()->runDispatchLoopUntil( Ms ); }
    juce::String f3( double v ) { return juce::String( v, 3 ); }

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

    // Counts device callbacks, to tell a stalled device from a silent engine.
    struct CallbackCounter : public juce::AudioIODeviceCallback
    {
        std::atomic<juce::int64> blocks{ 0 };
        void audioDeviceIOCallbackWithContext( const float* const*, int, float* const* Out, int NumOut, int NumSamples, const juce::AudioIODeviceCallbackContext& ) override
        {
            // The device manager sums every callback's output, so add silence.
            for( int c = 0; c < NumOut; ++c )
                if( Out[c] )
                    juce::FloatVectorOperations::clear( Out[c], NumSamples );
            ++blocks;
        }
        void audioDeviceAboutToStart( juce::AudioIODevice* ) override {}
        void audioDeviceStopped() override {}
    };
    CallbackCounter callbackCounter;

    // Waits (up to MaxMs) until the output is quiet; returns the last level seen.
    float waitForQuiet( juce::AudioDeviceManager::LevelMeter::Ptr Meter, int MaxMs )
    {
        float level = 1;
        for( int t = 0; t < MaxMs && ( level = measureOutput( Meter, 100 ) ) > 0.005f; t += 100 )
            ;
        return level;
    }

    constexpr int ticksPer16th = mt::defaultPPQN / 4; // 96 PPQN -> 24
    constexpr int numBars      = 2;
    const char*   markerName   = "kick_program_builder.txt";

    int countBeatNotes( const Kit& K )
    {
        int n = 0;
        for( auto& l : K.beat )
            for( auto c : l.steps )
                n += ( c != '.' );
        return n * numBars;
    }

    //==============================================================================
    bool buildKit( Results& R, const Kit& K, const juce::File& ProjectsDir, bool Reference )
    {
        auto& ai  = mt::getAudioInterface();
        auto  dir = ProjectsDir.getChildFile( K.name );
        R.section( "Build \"" + K.name + "\" -> " + dir.getFullPathName() );

        // Only ever replace a project this tool made.
        if( dir.exists() )
        {
            if( !R.check( dir.getChildFile( markerName ).existsAsFile(), "existing folder was made by this tool (safe to replace)" ) )
                return false;
            dir.deleteRecursively();
        }
        dir.getChildFile( "Samples" ).createDirectory();
        dir.getChildFile( "Export" ).createDirectory();

        // Resolve (and optionally copy) the samples first.
        juce::StringArray paths, layout;
        layout.add( K.name + " - drum program, bank A (pad numbers 1-16 = notes 0-15)" );
        layout.add( "BPM " + juce::String( K.bpm ) + ", " + juce::String( numBars ) + "-bar beat on Sequence1 / Drums" );
        layout.add( "Samples " + juce::String( Reference ? "referenced in place" : "copied into Samples/" ) + "\n" );
        for( auto& p : K.pads )
        {
            juce::File src( lib + p.file );
            if( !R.check( src.existsAsFile(), "source exists: " + src.getFileName() ) )
                return false;
            juce::File dst = Reference ? src : dir.getChildFile( "Samples" ).getChildFile( src.getFileName() );
            if( !Reference && !R.check( src.copyFileTo( dst ), "copied " + src.getFileName() ) )
                return false;
            paths.add( dst.getFullPathName() );
            layout.add( "A" + juce::String( p.pad + 1 ).paddedLeft( '0', 2 ) + "  " + p.label.paddedRight( ' ', 24 ) + ( p.muteGroup >= 0 ? " mg" + juce::String( p.muteGroup ) : "    " ) + "  " + src.getFileName() + "   <- " + src.getFullPathName() );
        }

        // New project = load a folder with no .kickPrj yet (as the UI does).
        auto projectDir = dir;
        if( !R.check( ai.loadProject( projectDir ), "new project loaded" ) )
            return false;
        pump( 200 );
        R.check( ai.getNumSequences() == 0 && ai.getNumPrograms() == 0, "starts empty", "seqs " + juce::String( ai.getNumSequences() ) + " progs " + juce::String( ai.getNumPrograms() ) );

        ai.setMasterBPM( K.bpm );
        const auto seq  = ai.createSequence( "Sequence1" );
        const auto prog = ai.createProgram( mt::MTProgramType::Drum, K.name );
        const auto trk  = ai.createTrack( seq, "Drums" );
        if( !R.check( seq >= 0 && prog >= 0 && trk >= 0, "created sequence, drum program, track", "seq " + juce::String( seq ) + " prog " + juce::String( prog ) + " trk " + juce::String( trk ) ) )
            return false;
        R.check( ai.setProgramToTrack( prog, seq, trk ) == 0, "program assigned to track" );
        ai.setSelectedSequenceAndTrack( seq, trk );
        ai.setSelectedProgram( prog );
        ai.setSequenceLength( seq, numBars );

        for( size_t i = 0; i < K.pads.size(); ++i )
        {
            ai.addAsset( paths[(int)i], mt::AssetTypes::Samples );
            ai.addSampleToProgram( prog, K.pads[i].pad, paths[(int)i] );
            if( K.pads[i].muteGroup >= 0 )
                ai.padAddToGroup( prog, K.pads[i].pad, K.pads[i].muteGroup );
        }
        pump( 1000 ); // samples load in the background
        R.check( ai.getNumProgramSamples( prog ) == (int)K.pads.size(), "samples on pads", juce::String( ai.getNumProgramSamples( prog ) ) + "/" + juce::String( (int)K.pads.size() ) );

        int notes = 0;
        for( int bar = 0; bar < numBars; ++bar )
            for( auto& lane : K.beat )
                for( int s = 0; s < lane.steps.length(); ++s )
                {
                    const auto c = lane.steps[s];
                    if( c == '.' )
                        continue;
                    const int vel = c == 'X' ? 127 : c == 'x' ? 100 : 60;
                    if( ai.addOrModifyNotesInTrack( seq, trk, lane.pad, ( bar * 16 + s ) * ticksPer16th, ticksPer16th, vel ) == 0 )
                        ++notes;
                }
        R.check( notes == countBeatNotes( K ), "beat written", juce::String( notes ) + " notes over " + juce::String( numBars ) + " bars" );

        pump( 300 );
        ai.saveProject();
        // No closeProject() here: the next loadProject() closes (and saves) it,
        // which also writes the sequence MIDI file. An explicit close followed
        // by a load left the device output silent for the next project.
        pump( 200 );

        dir.getChildFile( markerName ).replaceWithText( layout.joinIntoString( "\n" ) + "\n" );
        auto prj = dir.getChildFile( K.name + ".kickPrj" );
        R.check( prj.existsAsFile() && prj.getSize() > 1000, "saved " + prj.getFileName(), juce::String( prj.getSize() ) + " bytes" );
        for( auto& l : layout )
            R.note( l );
        return true;
    }

    //==============================================================================
    void verifyKit( Results& R, const Kit& K, const juce::File& ProjectsDir, bool Audio )
    {
        auto& ai  = mt::getAudioInterface();
        auto  dir = ProjectsDir.getChildFile( K.name );
        R.section( "Verify \"" + K.name + "\" (reloaded from " + dir.getFullPathName() + ")" );

        if( !R.check( dir.getChildFile( K.name + ".kickPrj" ).existsAsFile(), "project file exists" ) )
            return;
        auto projectDir = dir;
        if( !R.check( ai.loadProject( projectDir ), "project loads" ) )
            return;
        pump( 1500 ); // samples load in the background

        const auto prog = ai.getProgramIndexByName( K.name );
        R.check( ai.getNumSequences() == 1 && ai.getNumTracks( 0 ) == 1, "one sequence with one track", "seqs " + juce::String( ai.getNumSequences() ) + " tracks " + juce::String( ai.getNumTracks( 0 ) ) );
        if( !R.check( prog >= 0, "drum program present", "index " + juce::String( prog ) ) )
            return;
        R.check( ai.getNumProgramSamples( prog ) == (int)K.pads.size(), "program has all samples", juce::String( ai.getNumProgramSamples( prog ) ) + "/" + juce::String( (int)K.pads.size() ) );

        // Each pad points at the expected file and mute group.
        std::vector<mt::MTISample*> samples;
        ai.getProgramSamples( prog, samples );
        int ok = 0;
        juce::String bad;
        for( auto& p : K.pads )
        {
            bool found = false;
            for( auto* s : samples )
                if( s && (int)s->keyNote.get() == p.pad )
                    found = s->getSource().endsWith( juce::File( lib + p.file ).getFileName() ) && (int)s->muteGroup.get() == p.muteGroup && juce::File( s->getSource() ).existsAsFile();
            ok += found;
            if( !found )
                bad << p.pad << " ";
        }
        R.check( ok == (int)K.pads.size(), "pads map to the right files and mute groups", bad.isEmpty() ? juce::String( ok ) + " ok" : "bad pads " + bad );

        mt::MTSelected sel;
        sel.sequence = 0;
        sel.track    = 0;
        sel.program  = prog;
        R.check( ai.getNumNotes( sel ) == countBeatNotes( K ), "track notes restored", juce::String( ai.getNumNotes( sel ) ) + "/" + juce::String( countBeatNotes( K ) ) );
        R.check( ai.getSequenceLength( 0 ) == numBars, "sequence length", juce::String( ai.getSequenceLength( 0 ) ) + " bars" );

        if( !Audio )
            return;

        auto& dm = ai.getAudioDeviceManager();
        if( !R.check( dm.getCurrentAudioDevice() != nullptr, "audio device open" ) )
            return;
        auto       meter         = dm.getOutputLevelGetter();
        const auto expectedRate  = dm.getCurrentAudioDevice()->getCurrentSampleRate() / dm.getCurrentAudioDevice()->getCurrentBufferSizeSamples();

        // Hits every pad as AppMainmode does for a hardware pad press, then
        // plays the sequence. Returns the checks in their own Results.
        auto audioChecks = [&]() {
            Results A;
            ai.setSelectedSequenceAndTrack( 0, 0 );
            for( auto& p : K.pads )
            {
                const auto before = waitForQuiet( meter, 4000 );
                const auto b0     = callbackCounter.blocks.load();
                const auto t0     = juce::Time::getMillisecondCounterHiRes();
                ai.addLiveMidiMsg( ai.getSelected(), juce::MidiMessage::noteOn( 1, p.pad, (juce::uint8)127 ) );
                const auto peak = measureOutput( meter, 300 );
                ai.addLiveMidiMsg( ai.getSelected(), juce::MidiMessage::noteOff( 1, p.pad ) );
                const auto rate = ( callbackCounter.blocks.load() - b0 ) * 1000.0 / ( juce::Time::getMillisecondCounterHiRes() - t0 );
                A.check( peak > 0.05f && peak > before * 2, "pad A" + juce::String( p.pad + 1 ).paddedLeft( '0', 2 ) + " " + p.label + " sounds", "peak " + f3( peak ) + ", before " + f3( before ) + ", device callbacks " + juce::String( (int)rate ) + "/s of " + juce::String( (int)expectedRate ) );
            }
            waitForQuiet( meter, 4000 );

            ai.setSequencePlay( 0, true, true );
            const auto seqPeak = measureOutput( meter, (int)( 60000.0 / K.bpm * 4 * numBars ) );
            ai.setSequenceStop();
            A.check( seqPeak > 0.05f, "sequence plays the beat", "peak " + f3( seqPeak ) );
            waitForQuiet( meter, 4000 );
            return A;
        };

        auto A = audioChecks();
        if( A.failed > 0 )
        {
            // Seen intermittently on this machine: after a project load the
            // device keeps calling back at its normal rate but the engine's
            // output stays silent for that whole project. Reloading clears it,
            // so report the first attempt and judge the kit on a reload.
            R.note( "audio attempt 1 failed " + juce::String( A.failed ) + " checks (engine output silent); first failure:" );
            for( auto& l : A.lines )
                if( l.startsWith( "FAIL" ) )
                {
                    R.note( "  " + l );
                    break;
                }
            R.note( "reloading the project and retrying once" );
            ai.loadProject( projectDir );
            pump( 1500 );
            A = audioChecks();
        }
        R.lines.addArray( A.lines );
        R.passed += A.passed;
        R.failed += A.failed;
    }
} // namespace

//==============================================================================
class KickProgramBuilderApp : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "KickProgramBuilder"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }

    void initialise( const juce::String& CommandLine ) override
    {
#if JUCE_WINDOWS && defined( _DEBUG )
        for( int type : { _CRT_WARN, _CRT_ERROR, _CRT_ASSERT } )
            _CrtSetReportMode( type, _CRTDBG_MODE_DEBUG );
#endif
        juce::MessageManager::callAsync( [this, CommandLine]() { run( CommandLine ); } );
    }

    void shutdown() override { mt::getAudioInterface().shutdown(); }

private:
    void run( const juce::String& CommandLine )
    {
        auto exeDir = juce::File::getSpecialLocation( juce::File::currentExecutableFile ).getParentDirectory();

        bool                    reference = false, verifyOnly = false;
        juce::File              resultsFile = exeDir.getChildFile( "program_builder_results.txt" );
        juce::Array<juce::File> outDirs;
        for( auto& a : juce::StringArray::fromTokens( CommandLine, true ) )
        {
            auto arg = a.unquoted();
            if( arg == "--reference" )
                reference = true;
            else if( arg == "--verify-only" )
                verifyOnly = true;
            else if( arg.startsWith( "--results=" ) )
                resultsFile = juce::File( arg.fromFirstOccurrenceOf( "=", false, false ) );
            else if( arg.isNotEmpty() )
                outDirs.add( juce::File( arg ) );
        }
        if( outDirs.isEmpty() )
            outDirs.add( exeDir.getChildFile( "projects" ) );

        Results R;
        mt::getAudioInterface(); // the engine owns the AudIO memory pool
        pump( 1500 );            // the device opens asynchronously
        mt::getAudioInterface().getAudioDeviceManager().addAudioCallback( &callbackCounter );
        if( auto* dev = mt::getAudioInterface().getAudioDeviceManager().getCurrentAudioDevice() )
            R.note( "device: " + dev->getName() + " @ " + juce::String( dev->getCurrentSampleRate() ) + " Hz" );

        const auto all = kits();
        if( verifyOnly )
        {
            for( auto& out : outDirs )
                for( auto& k : all )
                    verifyKit( R, k, out, true );
        }
        else
        {
            for( auto& out : outDirs )
            {
                out.createDirectory();
                for( auto& k : all )
                    buildKit( R, k, out, reference );
            }
        }
        mt::getAudioInterface().getAudioDeviceManager().removeAudioCallback( &callbackCounter );
        mt::getAudioInterface().closeProject();

        // Verify each output folder in a fresh process. After several project
        // load/close cycles in one process the engine output was seen to stay
        // silent for a whole project (device still calling back normally), so
        // verifying in the building process gave false failures.
        if( !verifyOnly )
        {
            for( auto& out : outDirs )
            {
                auto              childResults = exeDir.getChildFile( "program_builder_verify.txt" );
                juce::StringArray args{ juce::File::getSpecialLocation( juce::File::currentExecutableFile ).getFullPathName(), "--verify-only", "--results=" + childResults.getFullPathName(), out.getFullPathName() };
                juce::ChildProcess child;
                childResults.deleteFile();
                if( R.check( child.start( args, 0 ), "started verifier for " + out.getFullPathName() ) )
                {
                    child.waitForProcessToFinish( 10 * 60 * 1000 );
                    const auto childFailed = (int)child.getExitCode();
                    R.lines.add( "\n---- verifier (fresh process) for " + out.getFullPathName() + " ----" );
                    R.lines.add( childResults.loadFileAsString() );
                    R.check( childFailed == 0 && childResults.existsAsFile(), "verifier passed", juce::String( childFailed ) + " failed" );
                }
            }
        }

        R.lines.add( "\n" + juce::String( R.passed ) + " passed, " + juce::String( R.failed ) + " failed" );
        resultsFile.replaceWithText( R.lines.joinIntoString( "\n" ) );

        setApplicationReturnValue( R.failed );
        quit();
    }
};

START_JUCE_APPLICATION( KickProgramBuilderApp )
