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

        setEngine( getEngineName() );
    }

    MTInstrumentProgram::~MTInstrumentProgram()
    {
        if( object )
            engine.removeObject( object->getId() );
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
        auto newEngine = InstrumentEngineRegistry::create( Name );
        if( !newEngine )
            return;

        // Switching engines starts from that engine's defaults.
        if( vTree.getProperty( IDs::instrumentEngine ).toString() != Name )
        {
            vTree.setProperty( IDs::instrumentEngine, Name, um );
            paramsTree().removeAllProperties( um );
        }

        params = newEngine->getParams();
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
        paramsTree().setProperty( juce::Identifier( p.id ), Value, um );
        object->setParam( Index, Value );
    }
} // namespace mt
