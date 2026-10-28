#include "mt_sample_editor_actions.h"
#include <filesystem>
#include <fstream>

namespace mt
{
    ///////////////////////////////////////////
    /// ///////// MTSampleEditUndoAction
    //////////////////////////////////////////
    MTSampleEditUndoAction::MTSampleEditUndoAction(MTSampleEditorActionsInterface* action)
        : actionImp(action) {}

    bool MTSampleEditUndoAction::restore(const juce::File& snapshot)
    {
        juce::TemporaryFile replacement(actionImp->getSourceFile());
        return snapshot.copyFileTo(replacement.getFile()) && replacement.overwriteTargetFileWithTemporary();
    }

    bool MTSampleEditUndoAction::perform()
    {
        if(after) return restore(after->getFile());
        const auto source = actionImp->getSourceFile();
        before = std::make_unique<juce::TemporaryFile>(source);
        if(!source.copyFileTo(before->getFile())) return false;
        if(!actionImp->execute()) return false;
        after = std::make_unique<juce::TemporaryFile>(source);
        if(source.copyFileTo(after->getFile())) return true;
        // Snapshot failure must not leave a changed sample behind a failed command.
        before->overwriteTargetFileWithTemporary();
        after.reset();
        return false;
    }

    bool MTSampleEditUndoAction::undo()
    {
        return before && restore(before->getFile());
    }

    int MTSampleEditUndoAction::getSizeInUnits()
    {
        const auto bytes = (before ? before->getFile().getSize() : 0) + (after ? after->getFile().getSize() : 0);
        return (int)juce::jlimit<juce::int64>(sizeof(*this), std::numeric_limits<int>::max(), bytes + sizeof(*this));
    }

    //////////////////////////////////////////////
    /// ///////// MTSampleEditorActionsInterface
    ///////////////////////////////////////////////
    MTSampleEditorActionsInterface::MTSampleEditorActionsInterface()
    {
        formatManager.registerBasicFormats();
    }

    void MTSampleEditorActionsInterface::initializeReaderWriter( juce::String& tempPath )
    {
        currentSample = 0;
        writeFailed = false;
        reader = formatManager.createReaderFor( sourceFile );
        if( !reader ) return;
        startSample = region.getStart();
        endSample = region.getEnd();
        totalSmaples = reader->lengthInSamples;
        startSample = juce::jlimit<juce::int64>(0, totalSmaples, startSample);
        endSample = juce::jlimit<juce::int64>(startSample, totalSmaples, endSample);

        juce::String fName = sourceFile.getFileNameWithoutExtension() + "_temp" + sourceFile.getFileExtension();
        juce::File tempFile = sourceFile.getSiblingFile( fName ).getNonexistentSibling();
        tempPath = tempFile.getFullPathName();

        auto outStream = createEditOutputStream(tempFile);
        if(!outStream) return;
        juce::WavAudioFormat format;
        writer = format.createWriterFor(outStream.get(), reader->sampleRate, reader->numChannels, (int)reader->bitsPerSample, {}, 0);
        if(writer) outStream.release();
    }

    juce::String MTSampleEditorActionsInterface::checkForDuplicateAndConstructNewPath( const juce::String file )
    {
        //conhstruct copy path
        juce::String fileName = file;
        if( !fileName.endsWith( ".wav" ) )
            fileName += ".wav";

        return sourceFile.getSiblingFile( fileName ).getNonexistentSibling().getFullPathName();
    }

    bool MTSampleEditorActionsInterface::extractFromSample( juce::int64 firstSample, juce::int64 lastSample, const juce::String& destFileName )
    {
        juce::String samplePath = checkForDuplicateAndConstructNewPath( destFileName );
        juce::File copyPath( samplePath );

        // create writer
        juce::WavAudioFormat format;
        juce::FileOutputStream* outStream = new juce::FileOutputStream( copyPath );
        juce::AudioFormatWriter* writerTmp = format.createWriterFor( outStream, reader->sampleRate, reader->numChannels, (int)reader->bitsPerSample, {}, 0 );
        if( !writerTmp ) { delete outStream; writeFailed = true; copyPath.deleteFile(); return false; }
        const bool complete = writerTmp->writeFromAudioReader(*reader, firstSample, lastSample - firstSample) && writerTmp->flush();
        delete writerTmp;
        if( !complete ) copyPath.deleteFile();
        writeFailed |= !complete;
        return complete;
    }

    void MTSampleEditorActionsInterface::executeprocessBetweenRegion()
    {
        // process from startSample to endSample
        regionProcessing = 1;
        juce::AudioSampleBuffer bufferTmp;
        int copySampleSize = sampleEditorBufferSize;
        writeFailed |= !writer->writeFromAudioReader( *reader, 0, startSample );

        currentSample = startSample;
        while( !writeFailed && performAction( bufferTmp, copySampleSize ) )
        {
            currentSample += copySampleSize;
            if( bufferTmp.getNumSamples() == copySampleSize )
                writeFailed |= !writer->writeFromAudioSampleBuffer( bufferTmp, 0, copySampleSize );
        }

        writeFailed |= !writer->writeFromAudioReader( *reader, endSample, (totalSmaples - endSample) );
    }

    int MTSampleEditorActionsInterface::read( juce::AudioSampleBuffer& buffer, juce::int64 /*startS*/, juce::int64 endS, juce::AudioFormatReader* r, juce::int64 current )
    {
        int numSamples = 0;
        if( currentSample >= endS )
            return numSamples;

        if( current + sampleEditorBufferSize >= endS )
        {
            numSamples = static_cast<int>(endS - currentSample);
        } else
        {
            numSamples = sampleEditorBufferSize;
        }

        buffer.setSize( (int)r->numChannels, numSamples );
        writeFailed |= !r->read( &buffer, 0, numSamples, current, true, true );

        return numSamples;
    }

    void MTSampleEditorActionsInterface::executeprocessOutofRegion()
    {
        // process from 0 to startSample
        regionProcessing = 0;
        juce::AudioSampleBuffer bufferTmp;
        int copySampleSize = sampleEditorBufferSize;
        while( !writeFailed && performAction( bufferTmp, copySampleSize ) )
        {
            currentSample += copySampleSize;
            if( bufferTmp.getNumSamples() == copySampleSize )
                writeFailed |= !writer->writeFromAudioSampleBuffer( bufferTmp, 0, copySampleSize );
        }

        // process from startSample to endSample
        writeFailed |= !writer->writeFromAudioReader( *reader, startSample, (endSample - startSample) );

        //process from endSample to totalSamples
        currentSample += endSample - startSample;
        regionProcessing = 2;
        while( !writeFailed && performAction( bufferTmp, copySampleSize ) )
        {
            currentSample += copySampleSize;
            if( bufferTmp.getNumSamples() == copySampleSize )
                writeFailed |= !writer->writeFromAudioSampleBuffer( bufferTmp, 0, copySampleSize );
        }
    }

    bool MTSampleEditorActionsInterface::cleanupReaderWriter( juce::String& tempPath )
    {
        const bool complete = !writeFailed && writer && writer->flush();
        delete writer;
        writer = nullptr;
        delete reader;
        reader = nullptr;
        juce::TemporaryFile temporary(sourceFile, juce::File(tempPath));
        return complete && temporary.overwriteTargetFileWithTemporary();
    }

    void MTSampleEditorActionsInterface::initialize( const MTSampleRegion& r, const std::list<juce::var>& p, juce::File& source )
    {
        region = r;
        params = p;
        sourceFile = source;
    }

    bool MTSampleEditorActionsInterface::execute()
    {
        juce::String tempPath;
        initializeReaderWriter( tempPath );
        if( reader == nullptr || writer == nullptr )
        {
            delete reader; reader = nullptr;
            delete writer; writer = nullptr;
            if( tempPath.isNotEmpty() ) juce::File(tempPath).deleteFile();

            return false;
        }

        juce::AudioSampleBuffer bufferTmp;
        int copySampleSize = sampleEditorBufferSize;
        if( type == noProcessing )
        {
            while( !writeFailed && performAction( bufferTmp, copySampleSize ) ) {}
            delete reader; reader = nullptr;
            delete writer; writer = nullptr;
            juce::File(tempPath).deleteFile();
            return !writeFailed;
        }
        if( type == processBetweenRegion )
        {
            executeprocessBetweenRegion();
        } else
        {
            executeprocessOutofRegion();
        }

        return cleanupReaderWriter( tempPath );
    }

    ///////////////////////////////////////////
    /// ///////// reverse action
    //////////////////////////////////////////
    bool MTSampleEditorReverseAction::performAction( juce::AudioSampleBuffer& buffer, int& numSamples )
    {
        if( currentSample >= endSample )
        {
            reverseCurrentSample = -1;
            return false;
        }
        if( reverseCurrentSample == -1 )
            reverseCurrentSample = endSample;

        numSamples = reverseCurrentSample - sampleEditorBufferSize <= startSample ? static_cast<int>(reverseCurrentSample - startSample) : sampleEditorBufferSize;
        reverseCurrentSample = reverseCurrentSample - sampleEditorBufferSize <= startSample ? startSample : reverseCurrentSample - sampleEditorBufferSize;
        buffer.setSize( (int)reader->numChannels, numSamples );

        writeFailed |= !reader->read( &buffer, 0, numSamples, reverseCurrentSample, true, true );
        buffer.reverse( 0, numSamples );

        return true;
    }

    ///////////////////////////////////////////
    /// ///////// normalise action
    //////////////////////////////////////////
    void MTSampleEditorNormaliseAction::initialize( const MTSampleRegion& r, const std::list<juce::var>& p, juce::File& source )
    {
        MTSampleEditorActionsInterface::initialize( r, p, source );
        juce::AudioFormatReader* readerTmp = formatManager.createReaderFor( sourceFile );

        startSample = region.getStart();
        endSample = region.getEnd();
        if( !readerTmp ) { gainFactor = 1.0f; return; }
        float lmin, lmax, rmin, rmax;
        readerTmp->readMaxLevels( startSample, endSample - startSample, lmin, lmax, rmin, rmax );

        auto maxLevel = juce::jmax( -lmin, lmax, -rmin, rmax );
        gainFactor = maxLevel > 0.0f ? CommonUtils::dbToGain( float( 0.0 ) ) / maxLevel : 1.0f;

        delete readerTmp;
    }

    bool MTSampleEditorNormaliseAction::performAction( juce::AudioSampleBuffer& buffer, int& numSamples )
    {
        if( currentSample >= endSample )
            return false;

        numSamples = static_cast<int>(endSample - startSample);
        buffer.setSize( (int)reader->numChannels, numSamples );

        writeFailed |= !reader->read( &buffer, 0, numSamples, currentSample, true, true );
        buffer.applyGain( gainFactor );

        return true;
    }

    ///////////////////////////////////////////
    /// ///////// fadein action
    //////////////////////////////////////////
    bool MTSampleEditorFadeInAction::performAction( juce::AudioSampleBuffer& buffer, int& numSamples )
    {
        if( currentSample >= endSample )
            return false;

        numSamples = static_cast<int>(endSample - startSample);
        buffer.setSize( (int)reader->numChannels, numSamples );
        writeFailed |= !reader->read( &buffer, 0, numSamples, currentSample, true, true );

        int inputCurveType = *params.begin();
        //AudioFadeCurve::Type curveType = AudioFadeCurve::linear;
        //if (inputCurveType == 0)
        //    curveType = AudioFadeCurve::linear;
        //else if (inputCurveType == 1)
        //    curveType = AudioFadeCurve::convex;
        //else if (inputCurveType == 2)
        //    curveType = AudioFadeCurve::concave;

        //AudioFadeCurve::applyCrossfadeSection(buffer,
        //    0, numSamples,
        //    curveType,
        //    0.0,
        //    1.0);

        return true;
    }

    ///////////////////////////////////////////
    /// ///////// fadeout action
    //////////////////////////////////////////
    bool MTSampleEditorFadeOutAction::performAction( juce::AudioSampleBuffer& buffer, int& numSamples )
    {
        if( currentSample >= endSample )
            return false;

        numSamples = static_cast<int>(endSample - startSample);
        buffer.setSize( (int)reader->numChannels, numSamples );
        writeFailed |= !reader->read( &buffer, 0, numSamples, currentSample, true, true );

        int curveTypeInput = *params.begin();
        //AudioFadeCurve::Type curveType = AudioFadeCurve::linear;
        //if( curveTypeInput == 0 )
        //    curveType = AudioFadeCurve::linear;
        //else if( curveTypeInput == 1 )
        //    curveType = AudioFadeCurve::convex;
        //else if( curveTypeInput == 2 )
        //    curveType = AudioFadeCurve::concave;

        //AudioFadeCurve::applyCrossfadeSection( buffer,
        //    0, numSamples,
        //    curveType,
        //    1.0,
        //    0.0 );

        return true;
    }

    /////////////////////////////////////////////////////////
    /// ///////// MTSampleEditorFromDestFileAction
    ////////////////////////////////////////////////////////
    MTSampleEditorFromDestFileAction::MTSampleEditorFromDestFileAction( juce::int64 start, juce::int64 end, juce::int64 start1, juce::int64 end1, juce::int64 ol, juce::File& df )
        : destStartSample( start )
        , destEndSample( end )
        , destStartSample1( start1 )
        , destEndSample1( end1 )
        , originalLength( ol )
        , destFile( df )
    {
    }

    bool MTSampleEditorFromDestFileAction::execute()
    {
        juce::String tempPath;
        initializeReaderWriter( tempPath );

        if( reader == nullptr || writer == nullptr )
        {
            if( reader )
                delete reader;
            if( writer )
                delete writer;

            return false;
        }

        //discard
        if( type == processOutofRegion )
        {
            startSample = 0;
            endSample = reader->lengthInSamples;
            MTSampleEditorActionsInterface::executeprocessOutofRegion();
        } else if( reader->lengthInSamples != originalLength ) //delete
        {
            endSample = startSample;
            executeprocessBetweenRegion();
        } else
        {
            executeprocessBetweenRegion();
        }
        cleanupReaderWriter( tempPath );

        return true;
    }

    void MTSampleEditorFromDestFileAction::deleteRegionFromDest( juce::AudioFormatReader* destReader )
    {
        juce::String fName = destFile.getFileNameWithoutExtension() + "_temp" + destFile.getFileExtension();
        juce::File tempFile = destFile.getSiblingFile( fName );

        juce::FileOutputStream* outStream = new juce::FileOutputStream( tempFile );
        juce::WavAudioFormat format;
        juce::AudioFormatWriter* writerTmp = format.createWriterFor( outStream, destReader->sampleRate, destReader->numChannels, (int)destReader->bitsPerSample, {}, 0 );
        writerTmp->writeFromAudioReader( *destReader, 0, destStartSample );

        delete writerTmp;
        //delete destReader;

        if( !destFile.deleteFile() )
        {
            jassertfalse;
        }

        tempFile.moveFileTo( destFile );
    }

    bool MTSampleEditorFromDestFileAction::performAction( juce::AudioSampleBuffer& buffer, int& numSamples )
    {
        if( regionProcessing == 0 && regionsProcessed == 1 )
            return false;
        if( (regionProcessing == 2 || regionProcessing == 1) && regionsProcessed == 2 )
            return false;

        numSamples = 0;
        juce::AudioFormatReader* destReder = formatManager.createReaderFor( destFile );
        juce::int64 destReadPos = 0;
        if( regionsProcessed == -1 )
        {
            destReadPos = destStartSample;
            numSamples = static_cast<int>(destEndSample - destStartSample);
            if( destEndSample1 == -1 && destStartSample1 == -1 )
            {
                regionsProcessed = 2;
            } else
            {
                regionsProcessed = 1;
            }
        } else if( regionsProcessed == 1 )
        {
            destReadPos = destStartSample1;
            numSamples = static_cast<int>(destEndSample1 - destStartSample1);
            regionsProcessed = 2;
        } else
        {
            jassertfalse;
        }

        if( destReder )
        {
            buffer.setSize( (int)reader->numChannels, numSamples );
            destReder->read( &buffer, 0, numSamples, destReadPos, true, true );

            if( regionsProcessed == 2 )
                deleteRegionFromDest( destReder );

            delete destReder;
        } else
        {
            jassertfalse;
            return false;
        }

        return true;
    }

    ///////////////////////////////////////////
    /// ///////// MTSampleEditorExtractAction
    //////////////////////////////////////////
    void MTSampleEditorExtractAction::initialize( const MTSampleRegion& r, const std::list<juce::var>& p, juce::File& source )
    {
        MTSampleEditorActionsInterface::initialize( r, p, source );
        type = noProcessing;
    }

    bool MTSampleEditorExtractAction::performAction( juce::AudioSampleBuffer& /*buffer*/, int& /*numSamples*/ )
    {
        extractFromSample( startSample, endSample, params.begin()->toString() );
        return false;
    }

    ///////////////////////////////////////////
    /// ///////// MTSampleEditorSilenceAction
    //////////////////////////////////////////

    bool MTSampleEditorSilenceAction::performAction( juce::AudioSampleBuffer& buffer, int& numSamples )
    {
        if( currentSample >= endSample )
            return false;

        numSamples = static_cast<int>(endSample - startSample);
        buffer.setSize( (int)reader->numChannels, numSamples );
        buffer.clear();

        return true;
    }

    void MTSampleEditorDiscardAction::initialize( const MTSampleRegion& r, const std::list<juce::var>& p, juce::File& source )
    {
        MTSampleEditorActionsInterface::initialize( r, p, source );
        type = processOutofRegion;
    }

    ///////////////////////////////////////////
    /// ///////// MTSampleEditorDiscardeAction
    //////////////////////////////////////////
    bool MTSampleEditorDiscardAction::performAction( juce::AudioSampleBuffer& /*buffer*/, int& numSamples )
    {
        if( (currentSample >= startSample && currentSample < endSample) || currentSample >= reader->lengthInSamples )
            return false;

        if( currentSample == 0 )
        {
            numSamples = static_cast<int>(startSample - currentSample);
        } else if( currentSample == endSample )
        {
            numSamples = static_cast<int>(reader->lengthInSamples - currentSample);
        } else
        {
            jassertfalse;
        }

        return true;
    }

    ///////////////////////////////////////////
    /// ///////// MTSampleEditorDiscardeAction
    //////////////////////////////////////////
    bool MTSampleEditorDeleteAction::performAction( juce::AudioSampleBuffer& /*buffer*/, int& numSamples )
    {
        if( currentSample >= endSample )
            return false;

        numSamples = static_cast<int>(endSample - startSample);
        return true;
    }

    ///////////////////////////////////////////
    /// ///////// MTSampleEditorGainChangeAction
    //////////////////////////////////////////

    void MTSampleEditorGainChangeAction::initialize( const MTSampleRegion& r, const std::list<juce::var>& p, juce::File& source )
    {
        region = r;
        params = p;
        sourceFile = source;
        gain = CommonUtils::dbToGain( *p.begin() );
    }

    bool MTSampleEditorGainChangeAction::performAction( juce::AudioSampleBuffer& buffer, int& numSamples )
    {
        if( currentSample >= endSample )
            return false;

        numSamples = read( buffer, startSample, endSample, reader, currentSample );
        buffer.applyGain( gain );

        return true;
    }

    ///////////////////////////////////////////
    /// ///////// MTSampleEditorCopyAction
    //////////////////////////////////////////
    void MTSampleEditorCopyAction::initialize( const MTSampleRegion& r, const std::list<juce::var>& p, juce::File& source )
    {
        region = r;
        params = p;
        sourceFile = source;
        setProcessType( noProcessing );
    }

    bool MTSampleEditorCopyAction::performAction( juce::AudioSampleBuffer& /*buffer*/, int& /*numSamples*/ )
    {
        juce::String samplePath = checkForDuplicateAndConstructNewPath( params.begin()->toString() );
        juce::File copyPath( samplePath );
        sourceFile.copyFileTo( copyPath );
        return false;
    }

    ///////////////////////////////////////////
    /// ///////// MTSampleEditorStereoToMonoAction
    //////////////////////////////////////////
    void MTSampleEditorStereoToMonoAction::initialize( const MTSampleRegion& r, const std::list<juce::var>& p, juce::File& source )
    {
        region = r;
        params = p;
        sourceFile = source;
        setProcessType( noProcessing );
    }

    bool MTSampleEditorStereoToMonoAction::performAction( juce::AudioSampleBuffer& /*buffer*/, int& /*numSamples*/ )
    {
        juce::String samplePath = checkForDuplicateAndConstructNewPath( params.back().toString() );
        juce::File monoFile( samplePath );

        // create writer
        juce::WavAudioFormat format;
        juce::FileOutputStream* outStream = new juce::FileOutputStream( monoFile );
        std::unique_ptr<juce::AudioFormatWriter> writerTmp( format.createWriterFor( outStream, reader->sampleRate, 1, (int)reader->bitsPerSample, {}, 0 ) );
        if( !writerTmp )
            jassertfalse;

        currentSample = 0;
        int copyType = params.front();
        while( currentSample < reader->lengthInSamples )
        {
            int copySize = sampleEditorBufferSize;
            if( currentSample + sampleEditorBufferSize >= reader->lengthInSamples )
                copySize = static_cast<int>(reader->lengthInSamples - currentSample);

            if( copyType == 0 || copyType == 1 )
            {
                juce::AudioSampleBuffer buff( 1, copySize );
                if( copyType == 0 )
                    writeFailed |= !reader->read( &buff, 0, copySize, currentSample, true, false );
                else
                    writeFailed |= !reader->read( &buff, 0, copySize, currentSample, false, true );
                writerTmp->writeFromAudioSampleBuffer( buff, 0, copySize );
            } else
            {
                juce::AudioSampleBuffer buff1( 1, copySize );
                juce::AudioSampleBuffer buff2( 1, copySize );
                writeFailed |= !reader->read( &buff1, 0, copySize, currentSample, true, false );
                writeFailed |= !reader->read( &buff2, 0, copySize, currentSample, false, true );
                buff1.addFrom( 0, 0, buff2, 0, 0, copySize );

                writerTmp->writeFromAudioSampleBuffer( buff1, 0, copySize );
            }

            currentSample += copySize;
        }
        writerTmp->flush();

        return false;
    }
}
