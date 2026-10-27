/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

namespace kick_ui
{
    namespace
    {
        // List order in the PAD LAYOUT box ("4ths", "3rds", "Sequential").
        constexpr std::array<mt::PadLayout::Mode, 3> layoutModes{ mt::PadLayout::Mode::Fourths, mt::PadLayout::Mode::Thirds, mt::PadLayout::Mode::Sequential };

        int layoutIndex( mt::PadLayout::Mode M )
        {
            for( size_t i = 0; i < layoutModes.size(); ++i )
                if( layoutModes[i] == M )
                    return (int)i;
            return 0;
        }
    } // namespace

    InstrumentProgramEdit::InstrumentProgramEdit()
        : ProgEditInterface( mt::MTProgramType::Plugin )
    {
        std::vector<juce::String> engineNames;
        for( auto& n : mt::InstrumentEngineRegistry::getNames() )
            engineNames.push_back( n );
        engine.setListOptions( engineNames, 0 );

        mt::utils::addAndMakeVisible( this, engine, preset, layout, keyMode, root, scale, prevPage, nextPage, pageLabel );

        for( size_t i = 0; i < knobs.size(); ++i )
        {
            auto& k = knobs[i];
            k       = std::make_unique<ui::KickRotary>( "P" + juce::String( (int)i + 1 ), juce::Slider::TextBoxBelow, true );
            k->showTitle( true, juce::Justification::centredTop );
            k->showValue( true, juce::Justification::centredBottom, juce::Slider::TextBoxBelow );
            addAndMakeVisible( *k );
        }

        pageLabel.setJustificationType( juce::Justification::centred );
        pageLabel.setColour( juce::Label::textColourId, ui::Theme::textColour( 1 ) );

        engine.onSelectionChanged = [this]( int, const juce::String& Name ) {
            if( auto* inst = instrument(); inst && inst->getEngineName() != Name )
            {
                inst->setEngine( Name );
                page = 0;
                refresh();
            }
        };

        // By name: the list may start with the current (edited) patch, and a refresh
        // re-announces the current entry, which must not reload it.
        preset.onSelectionChanged = [this]( int, const juce::String& Name ) {
            auto* inst = instrument();
            if( !inst || Name == inst->getPatchName() )
                return;
            if( const auto index = inst->getPresetNames().indexOf( Name ); index >= 0 )
                inst->loadPreset( index );
        };

        // Any layout control: write the whole layout back and relight the pads.
        auto onLayout = [this]( int, const juce::String& ) { storeLayout(); };
        layout.onSelectionChanged  = onLayout;
        keyMode.onSelectionChanged = onLayout;
        root.onSelectionChanged    = onLayout;
        scale.onSelectionChanged   = onLayout;

        prevPage.onClick = [this]() { showPage( page - 1 ); };
        nextPage.onClick = [this]() { showPage( page + 1 ); };
    }

    mt::MTInstrumentProgram* InstrumentProgramEdit::instrument() { return dynamic_cast<mt::MTInstrumentProgram*>( pIProg ); }

    juce::ValueTree InstrumentProgramEdit::layoutTree()
    {
        return pIProg ? pIProg->getState().getOrCreateChildWithName( mt::IDs::PAD_LAYOUT, pIProg->getUndoManager() ) : juce::ValueTree{};
    }

    void InstrumentProgramEdit::storeLayout()
    {
        if( !pIProg )
            return;

        mt::PadLayout l;
        l.mode       = layoutModes[(size_t)juce::jlimit( 0, (int)layoutModes.size() - 1, layout.getSelectedIndex() )];
        l.inKey      = keyMode.getSelectedIndex() == 0;
        l.root       = juce::jlimit( 0, 11, root.getSelectedIndex() );
        l.scaleIndex = juce::jmax( 0, scale.getSelectedIndex() );
        l.toTree( layoutTree(), pIProg->getUndoManager() );

        scale.setEnabled( l.inKey );
        contracts::DclSignals::refreshPadLeds();
    }

    void InstrumentProgramEdit::refresh()
    {
        auto* inst = instrument();
        if( !inst )
            return;

        engine.setSelected( inst->getEngineName(), juce::dontSendNotification );
        refreshPresets();

        const auto l = mt::PadLayout::fromTree( pIProg->getState().getChildWithName( mt::IDs::PAD_LAYOUT ) );
        layout.setSelected( layoutIndex( l.mode ), juce::dontSendNotification );
        keyMode.setSelected( l.inKey ? 0 : 1, juce::dontSendNotification );
        root.setSelected( l.root, juce::dontSendNotification );
        scale.setSelected( l.scaleIndex, juce::dontSendNotification );
        scale.setEnabled( l.inKey );

        showPage( page );
    }

    void InstrumentProgramEdit::refreshPresets()
    {
        auto*      inst  = instrument();
        const auto names = inst ? inst->getPresetNames() : juce::StringArray{};
        preset.setVisible( !names.isEmpty() );
        if( names.isEmpty() )
            return;

        std::vector<juce::String> options( names.begin(), names.end() );
        // An edited or renamed patch is not one of the presets; list it on top.
        const auto current = inst->getPatchName();
        if( !names.contains( current ) )
            options.insert( options.begin(), current );
        const auto selected = std::find( options.begin(), options.end(), current ) - options.begin();
        preset.setListOptions( options, (int)selected );
        resized();
    }

    int InstrumentProgramEdit::numPages() const
    {
        auto*      self = const_cast<InstrumentProgramEdit*>( this );
        const auto n    = self->instrument() ? (int)self->instrument()->getParams().size() : 0;
        return juce::jmax( 1, ( n + knobsPerPage - 1 ) / knobsPerPage );
    }

    void InstrumentProgramEdit::showPage( int Page )
    {
        page = juce::jlimit( 0, numPages() - 1, Page );

        auto*       inst   = instrument();
        const auto& params = inst ? inst->getParams() : std::vector<mt::InstrumentParam>{};

        for( int k = 0; k < knobsPerPage; ++k )
        {
            auto&      knob  = *knobs[(size_t)k];
            const auto index = page * knobsPerPage + k;
            knob.onValueChanged = nullptr;

            if( !inst || !juce::isPositiveAndBelow( index, (int)params.size() ) )
            {
                knob.setVisible( false );
                continue;
            }

            const auto& p = params[(size_t)index];
            juce::NormalisableRange<double> range( p.minValue, p.maxValue );
            // Frequencies and times feel right on a log-style curve.
            if( ( p.label == "Hz" || p.label == "s" ) && p.minValue > 0.f )
                range.setSkewForCentre( std::sqrt( (double)p.minValue * p.maxValue ) );

            knob.setNormalisableRange( range );
            knob.setNumDecimalPlacesToDisplay( p.maxValue - p.minValue >= 100.f ? 0 : 2 );
            knob.setTextValueSuffix( p.label.isNotEmpty() ? " " + p.label : juce::String() );
            knob.setTitle( "P" + juce::String( k + 1 ) + "  " + p.name.toUpperCase() );
            knob.setValue( inst->getParam( index ), juce::dontSendNotification );
            knob.onValueChanged = [this, index]( double Value ) {
                if( auto* i = instrument() )
                    i->setParam( index, (float)Value );
            };
            knob.setVisible( true );
        }

        pageLabel.setText( "PARAMS " + juce::String( page + 1 ) + " / " + juce::String( numPages() ), juce::dontSendNotification );
        prevPage.setEnabled( page > 0 );
        nextPage.setEnabled( page + 1 < numPages() );
        resized();
    }

    void InstrumentProgramEdit::handleCommand( CPktMgr::HWMsg_t& Msg )
    {
        if( !contracts::DclContractsHelper::isKnobMessage( Msg ) )
            return;

        // Q-link P1..P4 -> the four parameters on the current page, 1% per detent.
        const auto code = contracts::DclContractsHelper::getKnobCode( Msg );
        const auto k    = code - contracts::DeviceControlContracts::KnobSequence::EN_KNOB_1;
        if( !juce::isPositiveAndBelow( k, knobsPerPage ) )
            return;

        auto& knob = *knobs[(size_t)k];
        if( !knob.isVisible() )
            return;

        const auto steps = contracts::DclContractsHelper::getKnobValue( Msg );
        juce::MessageManager::callAsync( [this, k, steps]() {
            auto& kn = *knobs[(size_t)k];
            kn.setNormalized( kn.getNormalized() + 0.01 * steps, juce::sendNotification );
        } );
    }

    void InstrumentProgramEdit::paint( juce::Graphics& g )
    {
        auto b = getLocalBounds().reduced( 10 ).toFloat();
        g.setColour( ui::Theme::backgroundColour( 1 ) );
        g.fillRoundedRectangle( b, 5.f );
    }

    void InstrumentProgramEdit::resized()
    {
        auto b = getLocalBounds().reduced( 20 );

        // Top row: five selectors, six with a preset box.
        auto top = b.removeFromTop( juce::roundToInt( b.getHeight() * 0.36f ) );
        std::vector<juce::Component*> row{ &engine, &preset, &layout, &keyMode, &root, &scale };
        if( !preset.isVisible() )
            row.erase( row.begin() + 1 );
        const auto w = top.getWidth() / (int)row.size();
        for( auto* c : row )
            c->setBounds( top.removeFromLeft( w ).reduced( 6 ) );

        b.removeFromTop( 10 );

        // Page bar.
        auto bar = b.removeFromTop( 36 );
        prevPage.setBounds( bar.removeFromLeft( 60 ).reduced( 4 ) );
        nextPage.setBounds( bar.removeFromRight( 60 ).reduced( 4 ) );
        pageLabel.setBounds( bar );

        // Four knobs = four Q-links.
        const auto kw = b.getWidth() / knobsPerPage;
        for( auto& k : knobs )
            k->setBounds( b.removeFromLeft( kw ).reduced( 10 ) );
    }
} // namespace kick_ui
