/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

namespace mt
{
    MTInstrumentProgram::MTInstrumentProgram( AudIO::DawEngine& Engine, juce::ValueTree& VT, juce::UndoManager* UM )
        : MTIProgram( VT, UM ), engine( Engine )
    {
        auto obj = std::make_unique<InstrumentObject>( Engine, getName().toStdString() );
        object   = obj.get();
        Engine.createObject( std::move( obj ) );

        object->setProgram( this );
        object->initAudioRouting();
        object->connectOutputTo( engine.getBusManager()->getBusId( getOutput().toStdString() ) );
        object->setGainsForSend( getSendGains() );

        onNameChanged     = [this]() { object->setName( getName().toStdString() ); };
        onOutChanged      = [this]() { object->connectOutputTo( engine.getBusManager()->getBusId( getOutput().toStdString() ) ); };
        onSendGainChanged = [this]( int Channel, float Value ) { object->setGainForSend( (uint32_t)Channel, Value ); };

        auto inserts = VT.getOrCreateChildWithName( IDs::INSERTS, nullptr );
        MTIInsertsInterface::setup( inserts, UM, object );

        // Load directly, without touching the tree: a project naming an engine this
        // build lacks keeps its stored parameters (we fall back to the default engine).
        loadEngine( getEngineName() );
        if( !vTree.hasProperty( IDs::instrumentEngine ) )
            vTree.setProperty( IDs::instrumentEngine, loadedEngine, nullptr );
    }

    MTInstrumentProgram::~MTInstrumentProgram()
    {
        if( object )
        {
            // The object itself may be deleted later on the audio thread; release the
            // engine and the tree listeners here, on the message thread.
            object->setEngine( nullptr );
            object->setProgram( nullptr );
            engine.removeObject( object->getId() );
        }
    }

    void MTInstrumentProgram::noteProcess( juce::Array<juce::MidiMessage>& Msgs )
    {
        const bool muted = isProgramEffectivelyMuted();
        for( const auto& m : Msgs )
        {
            if( m.isAllNotesOff() || m.isAllSoundOff() || m.isResetAllControllers() )
                object->allNotesOff();
            else if( m.isPitchWheel() )
                object->pitchBend( 2.f * ( m.getPitchWheelValue() - 8192 ) / 8192.f ); // +-2 semitones
            else if( m.isControllerOfType( 1 ) )
                object->modWheel( m.getControllerValue() / 127.f );
            else if( muted && m.isNoteOff() )
                object->noteOff( m.getNoteNumber() ); // the base class drops everything while muted
        }

        MTIProgram::noteProcess( Msgs );
    }

    void MTInstrumentProgram::valueTreePropertyChanged( juce::ValueTree& Tree, const juce::Identifier& Property )
    {
        MTIProgram::valueTreePropertyChanged( Tree, Property );

        // Keep the engine in step with its tree, including undo/redo of these properties.
        if( Tree == vTree && Property == IDs::instrumentEngine )
        {
            if( getEngineName() != loadedEngine )
                loadEngine( getEngineName() );
        }
        else if( Tree.hasType( IDs::INSTRUMENT_PARAMS ) && Tree.getParent() == vTree )
        {
            for( int i = 0; i < (int)params.size(); ++i )
                if( Property.toString() == params[(size_t)i].id )
                    object->setParam( i, (float)Tree.getProperty( Property, params[(size_t)i].defaultValue ) );
        }
        else if( isPatchTree( Tree ) )
            sendPatch();
    }

    // Patch edits, presets and their undo/redo all arrive as tree changes.
    void MTInstrumentProgram::valueTreeChildAdded( juce::ValueTree& Parent, juce::ValueTree& Child )
    {
        if( isPatchTree( Parent ) || isPatchTree( Child ) )
            sendPatch();
    }

    void MTInstrumentProgram::valueTreeChildRemoved( juce::ValueTree& Parent, juce::ValueTree& Child, int )
    {
        // A removed patch is no longer attached, so test its type rather than its parent.
        if( isPatchTree( Parent ) || ( Parent == vTree && patchType.isValid() && Child.hasType( patchType ) ) )
            sendPatch();
    }

    void MTInstrumentProgram::valueTreeChildOrderChanged( juce::ValueTree& Parent, int, int )
    {
        if( isPatchTree( Parent ) )
            sendPatch();
    }

    bool MTInstrumentProgram::isPatchTree( const juce::ValueTree& Tree ) const
    {
        if( !patchType.isValid() )
            return false;
        for( auto t = Tree; t.isValid(); t = t.getParent() )
            if( t.hasType( patchType ) && t.getParent() == vTree )
                return true;
        return false;
    }

    void MTInstrumentProgram::sendPatch()
    {
        if( patchSendsBlocked > 0 || !patchType.isValid() )
            return;
        if( auto* e = object->getEngine() )
            e->setPatch( getPatch() );
    }

    juce::String MTInstrumentProgram::getPatchName() const
    {
        const auto patch = getPatch();
        if( patch.isValid() )
            return patch.getProperty( "name" ).toString();
        return presetNames.isEmpty() ? juce::String() : presetNames[0];
    }

    juce::ValueTree MTInstrumentProgram::getOrCreatePatch()
    {
        auto* e = object->getEngine();
        if( !patchType.isValid() || !e )
            return {};
        if( !getPatch().isValid() )
            vTree.appendChild( e->createPreset( 0 ), um ); // what it already plays: values only, no rebuild
        return getPatch();
    }

    void MTInstrumentProgram::loadPreset( int Index )
    {
        auto* e = object->getEngine();
        if( !patchType.isValid() || !e || !juce::isPositiveAndBelow( Index, presetNames.size() ) )
            return;

        // Remove and add as one change for the engine (one rebuild, not two).
        {
            const juce::ScopedValueSetter<int> block( patchSendsBlocked, patchSendsBlocked + 1 );
            if( auto old = getPatch(); old.isValid() )
                vTree.removeChild( old, um );
            vTree.appendChild( e->createPreset( Index ), um );
        }
        sendPatch();
    }

    juce::String MTInstrumentProgram::getEngineName() const
    {
        const juce::String name = vTree.getProperty( IDs::instrumentEngine );
        return InstrumentEngineRegistry::getNames().contains( name ) ? name : InstrumentEngineRegistry::getDefault();
    }

    juce::ValueTree MTInstrumentProgram::paramsTree()
    {
        return vTree.getOrCreateChildWithName( IDs::INSTRUMENT_PARAMS, um );
    }

    void MTInstrumentProgram::setEngine( const juce::String& Name )
    {
        if( !InstrumentEngineRegistry::getNames().contains( Name ) )
            return;

        // Switching engines starts from that engine's defaults. Both changes go through
        // the tree (and so the undo manager); valueTreePropertyChanged() loads the engine.
        if( vTree.getProperty( IDs::instrumentEngine ).toString() != Name )
        {
            paramsTree().removeAllProperties( um );
            if( auto patch = getPatch(); patch.isValid() )
                vTree.removeChild( patch, um );
            vTree.setProperty( IDs::instrumentEngine, Name, um );
        }
        else
            loadEngine( Name ); // same name: reload it fresh
    }

    void MTInstrumentProgram::loadEngine( const juce::String& Name )
    {
        auto newEngine = InstrumentEngineRegistry::create( Name );
        if( !newEngine )
            return;

        loadedEngine = Name;
        params       = newEngine->getParams();
        patchType    = newEngine->getPatchType();
        presetNames  = newEngine->getPresetNames();
        if( patchType.isValid() )
            newEngine->setPatch( getPatch() ); // before it goes live: nothing to interrupt
        object->setEngine( std::move( newEngine ) );
        applyStoredParams();
    }

    void MTInstrumentProgram::applyStoredParams()
    {
        auto tree = paramsTree();
        for( int i = 0; i < (int)params.size(); ++i )
        {
            const auto& p = params[(size_t)i];
            object->setParam( i, (float)tree.getProperty( juce::Identifier( p.id ), p.defaultValue ) );
        }
    }

    const std::vector<InstrumentParam>& MTInstrumentProgram::getParams() const { return params; }

    float MTInstrumentProgram::getParam( int Index ) const
    {
        if( !juce::isPositiveAndBelow( Index, (int)params.size() ) )
            return 0.f;
        const auto& p = params[(size_t)Index];
        return (float)vTree.getChildWithName( IDs::INSTRUMENT_PARAMS ).getProperty( juce::Identifier( p.id ), p.defaultValue );
    }

    void MTInstrumentProgram::setParam( int Index, float Value )
    {
        if( !juce::isPositiveAndBelow( Index, (int)params.size() ) )
            return;
        const auto& p = params[(size_t)Index];
        Value         = juce::jlimit( p.minValue, p.maxValue, Value );
        paramsTree().setProperty( juce::Identifier( p.id ), Value, um ); // -> valueTreePropertyChanged -> engine
    }
} // namespace mt
