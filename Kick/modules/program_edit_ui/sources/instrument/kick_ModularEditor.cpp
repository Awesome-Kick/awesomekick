/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

namespace kick_ui
{
    namespace
    {
        namespace mm = mt::modular;
        namespace PatchIDs = mm::PatchIDs;

        juce::Colour roleColour( mm::Role R )
        {
            switch( R )
            {
                case mm::Role::Source: return juce::Colour( 0xffc9821a );
                case mm::Role::Processor: return juce::Colour( 0xff2f78b0 );
                case mm::Role::Modulator: return juce::Colour( 0xff7552b8 );
            }
            return juce::Colours::grey;
        }

        juce::String formatValue( const mm::ParamSpec& P, double V )
        {
            if( !P.choices.empty() )
                return P.choices[(size_t)juce::jlimit( 0, (int)P.choices.size() - 1, juce::roundToInt( V - P.minValue ) )];
            // Note: juce::String( double, 0 ) means full precision, so whole numbers are rounded here.
            if( P.label == "Hz" )
                return V >= 1000.0 ? juce::String( V / 1000.0, V >= 10000.0 ? 1 : 2 ) + " kHz" : ( V < 10.0 ? juce::String( V, 2 ) : juce::String( juce::roundToInt( V ) ) ) + " Hz";
            if( P.label == "s" )
                return V < 1.0 ? juce::String( juce::roundToInt( V * 1000.0 ) ) + " ms" : juce::String( V, 2 ) + " s";
            if( P.label == "ms" )
                return V < 10.0 ? juce::String( V, 2 ) + " ms" : juce::String( juce::roundToInt( V ) ) + " ms";
            if( P.curve == mm::Curve::Steps )
                return juce::String( juce::roundToInt( V ) ) + ( P.label.isNotEmpty() ? " " + P.label : juce::String() );
            return juce::String( V, 2 ) + ( P.label.isNotEmpty() ? " " + P.label : juce::String() );
        }

        // The knob a module shows on the grid.
        juce::String summaryParam( const juce::String& Type )
        {
            static const std::map<juce::String, juce::String> key = {
                { "osc", "wave" },      { "noise", "color" }, { "filter", "cutoff" }, { "vca", "gain" },   { "adsr", "decay" }, { "lfo", "rate" },
                { "random", "polarity" }, { "math", "gain" },   { "drive", "drive" },   { "delay", "time" }, { "crush", "bits" }, { "pan", "pan" },
            };
            const auto it = key.find( Type );
            return it != key.end() ? it->second : juce::String();
        }

        const juce::String fromArrow = juce::String( juce::CharPointer_UTF8( " \xe2\x86\x90 " ) );
    } // namespace

    //==============================================================================
    class ModularEditor::Grid : public juce::Component
    {
    public:
        explicit Grid( ModularEditor& E ) : editor( E ) {}

        juce::Rectangle<float> cellBounds( int Col, int Row ) const
        {
            const auto cw = (float)getWidth() / mm::gridColumns, ch = (float)getHeight() / mm::gridRows;
            return { Col * cw, Row * ch, cw, ch };
        }

        bool cellAt( juce::Point<float> P, int& Col, int& Row ) const
        {
            Col = (int)std::floor( P.x / ( (float)getWidth() / mm::gridColumns ) );
            Row = (int)std::floor( P.y / ( (float)getHeight() / mm::gridRows ) );
            return juce::isPositiveAndBelow( Col, mm::gridColumns ) && juce::isPositiveAndBelow( Row, mm::gridRows );
        }

        void paint( juce::Graphics& g ) override
        {
            const auto patch = editor.view();
            const auto read  = editor.reader();

            // Empty cells.
            for( int c = 0; c < mm::gridColumns; ++c )
                for( int r = 0; r < mm::gridRows; ++r )
                {
                    const auto cell = cellBounds( c, r ).reduced( 3.f );
                    g.setColour( ui::Theme::backgroundColour( 1 ).brighter( 0.08f ) );
                    g.fillRoundedRectangle( cell, 4.f );
                }

            // Signal flow: a line down each column that holds audio modules.
            for( int c = 0; c < mm::gridColumns; ++c )
            {
                int top = -1;
                for( int r = 0; r < mm::gridRows && top < 0; ++r )
                    if( const auto* s = mm::PatchEditor::specOf( read.moduleAt( c, r ) ); s && s->role != mm::Role::Modulator )
                        top = r;
                if( top < 0 )
                    continue;
                const auto x  = cellBounds( c, 0 ).getCentreX();
                const auto y0 = cellBounds( c, top ).getCentreY();
                const auto y1 = (float)getHeight() - 2.f;
                g.setColour( ui::Theme::textColour( 1 ).withAlpha( 0.45f ) );
                g.drawLine( x, y0, x, y1, 2.f );
                juce::Path arrow;
                arrow.addTriangle( x - 6.f, y1 - 8.f, x + 6.f, y1 - 8.f, x, y1 );
                g.fillPath( arrow );
            }

            // Which modules relate to the selection through modulation.
            const auto        selected = editor.selectedModule();
            std::set<int>     related;
            const juce::String selectedSource = selected.isValid() ? mm::PatchBuilder::module( (int)selected.getProperty( PatchIDs::id ) ) : juce::String();
            for( const auto& m : patch )
                for( const auto& r : m )
                {
                    if( !r.hasType( PatchIDs::MOD ) )
                        continue;
                    const auto source = r.getProperty( PatchIDs::source ).toString();
                    if( m == selected && source.startsWith( "module:" ) )
                        related.insert( source.fromFirstOccurrenceOf( ":", false, false ).getIntValue() ); // its modulators
                    if( selectedSource.isNotEmpty() && source == selectedSource )
                        related.insert( (int)m.getProperty( PatchIDs::id ) ); // what it modulates
                }

            // Modules.
            for( const auto& m : patch )
            {
                const auto* spec = mm::PatchEditor::specOf( m );
                if( !spec )
                    continue;
                const int id = m.getProperty( PatchIDs::id );
                if( dragging && id == dragId )
                    continue;
                drawModule( g, m, *spec, cellBounds( m.getProperty( PatchIDs::col ), m.getProperty( PatchIDs::row ) ).reduced( 3.f ), related.count( id ) > 0 );
            }

            // Selection.
            g.setColour( juce::Colours::white );
            g.drawRoundedRectangle( cellBounds( editor.selCol, editor.selRow ).reduced( 2.f ), 5.f, 2.5f );

            // A module being dragged follows the finger.
            if( dragging )
                if( auto m = read.module( dragId ); m.isValid() )
                    if( const auto* spec = mm::PatchEditor::specOf( m ) )
                    {
                        auto cell = cellBounds( 0, 0 ).reduced( 3.f ).withCentre( dragPos );
                        g.setOpacity( 0.8f );
                        drawModule( g, m, *spec, cell, false );
                        g.setOpacity( 1.f );
                    }
        }

        void mouseDown( const juce::MouseEvent& e ) override
        {
            int c, r;
            dragging = false;
            dragId   = 0;
            if( !cellAt( e.position, c, r ) )
                return;
            wasSelected = c == editor.selCol && r == editor.selRow;
            editor.selectCell( c, r );
            if( auto m = editor.reader().moduleAt( c, r ); m.isValid() )
                dragId = m.getProperty( PatchIDs::id );
        }

        void mouseDrag( const juce::MouseEvent& e ) override
        {
            if( dragId && e.getDistanceFromDragStart() > 8 )
            {
                dragging = true;
                dragPos  = e.position;
                repaint();
            }
        }

        void mouseUp( const juce::MouseEvent& e ) override
        {
            int c, r;
            const bool inside = cellAt( e.position, c, r );
            if( dragging )
            {
                dragging = false;
                if( inside )
                    editor.moveModule( c, r );
                repaint();
            }
            else if( inside && wasSelected && !dragId && c == editor.selCol && r == editor.selRow )
                editor.pickModuleType(); // tap an empty cell again to add a module
        }

    private:
        void drawModule( juce::Graphics& g, const juce::ValueTree& M, const mm::ModuleSpec& Spec, juce::Rectangle<float> Cell, bool Related )
        {
            const auto colour = roleColour( Spec.role );
            g.setColour( colour.darker( 0.35f ) );
            g.fillRoundedRectangle( Cell, 5.f );
            g.setColour( Related ? juce::Colours::white.withAlpha( 0.85f ) : colour.brighter( 0.2f ) );
            g.drawRoundedRectangle( Cell, 5.f, Related ? 2.f : 1.f );

            const int  id   = M.getProperty( PatchIDs::id );
            const auto read = editor.reader();
            auto       text = Cell.reduced( 4.f, 3.f );

            g.setColour( juce::Colours::white );
            g.setFont( juce::Font( 14.f, juce::Font::bold ) );
            g.drawFittedText( read.moduleLabel( id ), text.removeFromTop( text.getHeight() * 0.5f ).toNearestInt(), juce::Justification::centredBottom, 1, 0.8f );

            const auto key = summaryParam( Spec.type );
            if( const auto p = Spec.findParam( key ); p >= 0 )
            {
                juce::String value = formatValue( Spec.params[(size_t)p], read.getValue( id, key ) );
                if( Spec.type == "delay" && read.getValue( id, "track" ) > 0.5f )
                    value = "Tracked";
                g.setColour( juce::Colours::white.withAlpha( 0.75f ) );
                g.setFont( juce::Font( 12.f ) );
                g.drawFittedText( value, text.toNearestInt(), juce::Justification::centredTop, 1, 0.8f );
            }

            // ~ = this module's knobs are modulated.
            int mods = 0;
            for( const auto& r : M )
                mods += r.hasType( PatchIDs::MOD ) ? 1 : 0;
            if( mods )
            {
                g.setColour( roleColour( mm::Role::Modulator ).brighter( 0.6f ) );
                g.setFont( juce::Font( 13.f, juce::Font::bold ) );
                g.drawText( "~" + juce::String( mods ), Cell.reduced( 4.f, 2.f ).toNearestInt(), juce::Justification::topRight, false );
            }
        }

        ModularEditor&     editor;
        int                dragId{ 0 };
        bool               dragging{ false }, wasSelected{ false };
        juce::Point<float> dragPos;
    };

    //==============================================================================
    ModularEditor::ModularEditor()
        : grid( std::make_unique<Grid>( *this ) )
    {
        addAndMakeVisible( *grid );
        mt::utils::addAndMakeVisible( this, back, title, voices, moduleTitle, hint, modeButton, addModuleButton, deleteButton, addModButton, removeModButton, prevPage, nextPage, pageLabel );

        for( auto* l : { &title, &moduleTitle, &pageLabel } )
            l->setColour( juce::Label::textColourId, ui::Theme::textColour( 1 ) );
        title.setFontHeight( 20.f );
        moduleTitle.setFontHeight( 20.f );
        hint.setColour( juce::Label::textColourId, ui::Theme::textColour( 1 ).withAlpha( 0.7f ) );
        hint.setJustificationType( juce::Justification::centred );
        pageLabel.setJustificationType( juce::Justification::centred );

        for( size_t i = 0; i < knobs.size(); ++i )
        {
            auto& k = knobs[i];
            k       = std::make_unique<ui::KickRotary>( "P" + juce::String( (int)i + 1 ), juce::Slider::TextBoxBelow, true );
            k->showTitle( true, juce::Justification::centredTop );
            k->showValue( true, juce::Justification::centredBottom, juce::Slider::TextBoxBelow );
            k->onDragStart = [this, i]() {
                if( mode == Mode::Mods )
                {
                    selectedMod = page * knobsPerPage + (int)i;
                    refreshInspector();
                }
            };
            addAndMakeVisible( *k );
        }

        back.onClick            = [this]() { if( onClose ) onClose(); };
        voices.onClick          = [this]() { pickVoices(); };
        modeButton.onClick      = [this]() { showMods( mode == Mode::Params ); };
        addModuleButton.onClick = [this]() { pickModuleType(); };
        deleteButton.onClick    = [this]() { deleteModule(); };
        addModButton.onClick    = [this]() { pickModulation(); };
        removeModButton.onClick = [this]() {
            const auto mods = selectedMods();
            if( juce::isPositiveAndBelow( selectedMod, (int)mods.size() ) )
            {
                edit().removeMod( mods[(size_t)selectedMod] );
                selectedMod = -1;
            }
        };
        prevPage.onClick = [this]() { showPage( page - 1 ); };
        nextPage.onClick = [this]() { showPage( page + 1 ); };
    }

    ModularEditor::~ModularEditor() { setProgram( nullptr ); }

    void ModularEditor::setProgram( mt::MTInstrumentProgram* Program )
    {
        if( Program == program )
        {
            refresh(); // same program: keep the selection and page
            return;
        }
        if( programState.isValid() )
            programState.removeListener( this );
        program      = Program;
        programState = Program ? Program->getState() : juce::ValueTree{};
        if( programState.isValid() )
            programState.addListener( this );

        defaultPatch = mm::createPreset( 0 );
        page         = 0;
        selectedMod  = -1;
        refresh();
    }

    juce::ValueTree ModularEditor::view() const
    {
        const auto stored = program ? program->getPatch() : juce::ValueTree{};
        return stored.isValid() ? stored : defaultPatch;
    }

    mm::PatchEditor ModularEditor::edit( const juce::String& Gesture )
    {
        if( !program )
            return { {}, nullptr };

        auto*      um  = program->getUndoManager();
        const auto now = juce::Time::getMillisecondCounter();
        if( um && ( Gesture.isEmpty() || Gesture != lastGesture || now - lastEditMs > 1500 ) )
            um->beginNewTransaction();
        lastGesture = Gesture;
        lastEditMs  = now;
        return { program->getOrCreatePatch(), um };
    }

    juce::ValueTree ModularEditor::selectedModule() const { return reader().moduleAt( selCol, selRow ); }

    std::vector<juce::ValueTree> ModularEditor::selectedMods() const
    {
        std::vector<juce::ValueTree> mods;
        for( const auto& r : selectedModule() )
            if( r.hasType( PatchIDs::MOD ) )
                mods.push_back( r );
        return mods;
    }

    //==============================================================================
    void ModularEditor::selectCell( int Col, int Row )
    {
        Col = juce::jlimit( 0, mm::gridColumns - 1, Col );
        Row = juce::jlimit( 0, mm::gridRows - 1, Row );
        if( Col != selCol || Row != selRow )
        {
            selCol      = Col;
            selRow      = Row;
            page        = 0;
            selectedMod = -1;
        }
        refresh();
    }

    void ModularEditor::addModule( const juce::String& Type )
    {
        edit().addModule( Type, selCol, selRow );
        mode = Mode::Params;
        page = 0;
    }

    void ModularEditor::deleteModule()
    {
        if( auto m = selectedModule(); m.isValid() )
            edit().removeModule( m.getProperty( PatchIDs::id ) );
    }

    void ModularEditor::moveModule( int Col, int Row )
    {
        if( auto m = selectedModule(); m.isValid() )
        {
            edit().moveModule( m.getProperty( PatchIDs::id ), Col, Row );
            selCol = juce::jlimit( 0, mm::gridColumns - 1, Col );
            selRow = juce::jlimit( 0, mm::gridRows - 1, Row );
        }
    }

    void ModularEditor::addModulation( const juce::String& Param, const juce::String& Source, float Depth )
    {
        auto m = selectedModule();
        if( !m.isValid() )
            return;
        edit().addMod( m.getProperty( PatchIDs::id ), Param, Source, Depth );
        mode        = Mode::Mods;
        selectedMod = (int)selectedMods().size() - 1; // the new one is last
        page        = juce::jmax( 0, selectedMod ) / knobsPerPage;
    }

    void ModularEditor::showMods( bool Mods )
    {
        mode        = Mods ? Mode::Mods : Mode::Params;
        page        = 0;
        selectedMod = -1;
        refreshInspector();
    }

    //==============================================================================
    void ModularEditor::pick( const juce::String& Title, const juce::StringArray& Options, std::function<void( int )> OnPick )
    {
        std::vector<juce::String> options( Options.begin(), Options.end() );
        picker.setContent( options, 0, juce::dontSendNotification );
        picker.show( Title, [this, OnPick, n = Options.size()]( int Result ) {
            // A tapped option closes the list with its index; APPLY keeps the highlighted one.
            const auto index = Result == UI::APPLY ? picker.getSelectedIndex() : Result;
            if( juce::isPositiveAndBelow( index, n ) && OnPick )
                OnPick( index );
        } );
    }

    void ModularEditor::pickModuleType()
    {
        if( selectedModule().isValid() || !program )
            return;
        juce::StringArray names, types;
        for( const auto& s : mm::getModuleSpecs() )
        {
            names.add( s.name );
            types.add( s.type );
        }
        pick( "ADD MODULE", names, [this, types]( int Index ) { addModule( types[Index] ); } );
    }

    void ModularEditor::pickModulation()
    {
        const auto  m    = selectedModule();
        const auto* spec = mm::PatchEditor::specOf( m );
        if( !spec )
            return;

        juce::StringArray params;
        for( const auto& p : spec->params )
            params.add( p.name );

        const auto self = mm::PatchBuilder::module( m.getProperty( PatchIDs::id ) );
        pick( "MODULATE WHICH KNOB?", params, [this, spec, self]( int P ) {
            const auto param = spec->params[(size_t)P].id;
            juce::StringArray ids, names;
            for( const auto& [id, name] : reader().getSources() )
                if( id != self ) // a modulator cannot modulate itself
                {
                    ids.add( id );
                    names.add( name );
                }
            // Second list once the first has closed.
            juce::MessageManager::callAsync( [this, param, ids, names]() {
                pick( "MODULATE " + param.toUpperCase() + " FROM", names, [this, param, ids]( int S ) { addModulation( param, ids[S] ); } );
            } );
        } );
    }

    void ModularEditor::pickVoices()
    {
        if( !program )
            return;
        juce::StringArray options;
        for( int v = 1; v <= mm::maxVoices; ++v )
            options.add( juce::String( v ) );
        pick( "VOICES", options, [this]( int Index ) { edit().setVoices( Index + 1 ); } );
    }

    //==============================================================================
    void ModularEditor::refresh()
    {
        if( !program )
            return;
        const auto patch = view();
        title.setText( "MODULAR  " + patch.getProperty( PatchIDs::name ).toString(), juce::dontSendNotification );
        voices.setButtonText( "VOICES " + juce::String( reader().getVoices() ) );
        grid->repaint();
        refreshInspector();
    }

    int ModularEditor::numItems() const
    {
        if( mode == Mode::Mods )
            return (int)selectedMods().size();
        const auto* spec = mm::PatchEditor::specOf( selectedModule() );
        return spec ? (int)spec->params.size() : 0;
    }

    void ModularEditor::refreshInspector()
    {
        const auto  m     = selectedModule();
        const auto* spec  = mm::PatchEditor::specOf( m );
        const auto  empty = spec == nullptr;
        const auto  read  = reader();

        if( empty )
        {
            moduleTitle.setText( "Empty cell", juce::dontSendNotification );
            hint.setText( "Tap + MODULE, or tap the cell again, to add a module.\n\n"
                          "Audio runs down each column and the columns are mixed. "
                          "Envelopes, LFOs and Random can go anywhere: they modulate knobs.",
                          juce::dontSendNotification );
        }
        else
            moduleTitle.setText( read.moduleLabel( m.getProperty( PatchIDs::id ) ), juce::dontSendNotification );

        hint.setVisible( empty );
        addModuleButton.setVisible( empty );
        for( auto* c : std::initializer_list<juce::Component*>{ &modeButton, &deleteButton, &addModButton, &prevPage, &nextPage, &pageLabel } )
            c->setVisible( !empty );
        removeModButton.setVisible( !empty && mode == Mode::Mods );

        const auto mods = selectedMods();
        modeButton.setButtonText( mode == Mode::Params ? "MODS (" + juce::String( (int)mods.size() ) + ")" : "PARAMS" );

        const auto pages = juce::jmax( 1, ( numItems() + knobsPerPage - 1 ) / knobsPerPage );
        page             = juce::jlimit( 0, pages - 1, page );
        pageLabel.setText( ( mode == Mode::Params ? "PARAMS " : "MODS " ) + juce::String( page + 1 ) + " / " + juce::String( pages ), juce::dontSendNotification );
        prevPage.setEnabled( page > 0 );
        nextPage.setEnabled( page + 1 < pages );

        // REMOVE acts on the modulation last touched (its knob title starts with ">").
        const auto modSelected = juce::isPositiveAndBelow( selectedMod, (int)mods.size() );
        removeModButton.setButtonText( modSelected && selectedMod / knobsPerPage == page ? "REMOVE P" + juce::String( selectedMod % knobsPerPage + 1 ) : "REMOVE MOD" );
        removeModButton.setEnabled( modSelected );

        for( int k = 0; k < knobsPerPage; ++k )
        {
            auto&      knob  = *knobs[(size_t)k];
            const auto index = page * knobsPerPage + k;
            if( empty || index >= numItems() )
            {
                knob.setVisible( false );
                continue;
            }
            knob.setVisible( true );
            if( knob.isMouseButtonDown() )
                continue; // being turned: leave it alone

            knob.onValueChanged = nullptr;
            if( mode == Mode::Params )
            {
                const auto& p = spec->params[(size_t)index];
                juce::NormalisableRange<double> range( p.minValue, p.maxValue, p.curve == mm::Curve::Steps ? 1.0 : 0.0 );
                if( p.curve == mm::Curve::Log )
                    range.setSkewForCentre( std::sqrt( (double)p.minValue * p.maxValue ) );
                knob.setNormalisableRange( range );
                knob.textFromValueFunction = [p]( double V ) { return formatValue( p, V ); };

                int modded = 0;
                for( const auto& r : mods )
                    modded += r.getProperty( PatchIDs::param ).toString() == p.id ? 1 : 0;
                knob.setTitle( "P" + juce::String( k + 1 ) + "  " + p.name.toUpperCase() + ( modded ? "  ~" : "" ) );
                knob.setValue( read.getValue( m.getProperty( PatchIDs::id ), p.id ), juce::dontSendNotification );
            }
            else
            {
                const auto& r     = mods[(size_t)index];
                const auto  param = r.getProperty( PatchIDs::param ).toString();
                const auto  pi    = spec->findParam( param );
                const auto  name  = pi >= 0 ? spec->params[(size_t)pi].name.toUpperCase() : param;
                knob.setNormalisableRange( { -1.0, 1.0 } );
                knob.textFromValueFunction = []( double V ) { return ( V > 0 ? "+" : "" ) + juce::String( juce::roundToInt( V * 100.0 ) ) + "%"; };
                knob.setTitle( juce::String( index == selectedMod ? "> " : "" ) + "P" + juce::String( k + 1 ) + "  " + name + fromArrow + read.sourceLabel( r.getProperty( PatchIDs::source ) ).toUpperCase() );
                knob.setValue( (double)r.getProperty( PatchIDs::depth ), juce::dontSendNotification );
            }
            knob.updateText();
            knob.onValueChanged = [this, k]( double Value ) { applyKnob( k, Value ); };
        }
        resized();
    }

    void ModularEditor::showPage( int Page )
    {
        page = Page;
        refreshInspector();
    }

    void ModularEditor::applyKnob( int Knob, double Value )
    {
        const auto  m     = selectedModule();
        const auto* spec  = mm::PatchEditor::specOf( m );
        const auto  index = page * knobsPerPage + Knob;
        if( !spec )
            return;
        const int id = m.getProperty( PatchIDs::id );

        if( mode == Mode::Params )
        {
            if( juce::isPositiveAndBelow( index, (int)spec->params.size() ) )
            {
                const auto& p = spec->params[(size_t)index].id;
                edit( "param:" + juce::String( id ) + ":" + p ).setValue( id, p, (float)Value );
            }
        }
        else
        {
            const auto mods = selectedMods();
            if( juce::isPositiveAndBelow( index, (int)mods.size() ) )
            {
                selectedMod = index;
                edit( "depth:" + juce::String( id ) + ":" + juce::String( index ) ).setModDepth( mods[(size_t)index], (float)Value );
            }
        }
    }

    void ModularEditor::turnKnob( int Knob, int Steps )
    {
        if( Knob < 0 )
        {
            // The centre knob walks the cells, row by row.
            const auto cell = juce::jlimit( 0, mm::gridColumns * mm::gridRows - 1, selRow * mm::gridColumns + selCol + Steps );
            selectCell( cell % mm::gridColumns, cell / mm::gridColumns );
            return;
        }
        if( !juce::isPositiveAndBelow( Knob, knobsPerPage ) || !knobs[(size_t)Knob]->isVisible() )
            return;

        auto&      knob     = *knobs[(size_t)Knob];
        const auto interval = knob.getNormalisableRange().interval;
        if( interval > 0.0 )
            knob.setValue( knob.getValue() + Steps * interval, juce::sendNotification ); // one detent = one choice
        else
            knob.setNormalized( knob.getNormalized() + 0.01 * Steps, juce::sendNotification );
    }

    //==============================================================================
    void ModularEditor::paint( juce::Graphics& g )
    {
        g.setColour( ui::Theme::backgroundColour( 1 ) );
        g.fillRoundedRectangle( getLocalBounds().reduced( 10 ).toFloat(), 5.f );
    }

    void ModularEditor::resized()
    {
        auto b = getLocalBounds().reduced( 18 );

        auto top = b.removeFromTop( 42 );
        back.setBounds( top.removeFromLeft( 110 ).reduced( 3 ) );
        voices.setBounds( top.removeFromRight( 130 ).reduced( 3 ) );
        title.setBounds( top.reduced( 12, 0 ) );
        b.removeFromTop( 6 );

        grid->setBounds( b.removeFromLeft( juce::roundToInt( b.getWidth() * 0.56f ) ) );
        b.removeFromLeft( 14 );

        auto head = b.removeFromTop( 36 );
        modeButton.setBounds( head.removeFromRight( 110 ).reduced( 3 ) );
        moduleTitle.setBounds( head );

        auto actions = b.removeFromBottom( 42 );
        addModuleButton.setBounds( actions.reduced( 3 ) );
        const auto aw = actions.getWidth() / 3;
        deleteButton.setBounds( actions.removeFromLeft( aw ).reduced( 3 ) );
        addModButton.setBounds( actions.removeFromLeft( aw ).reduced( 3 ) );
        removeModButton.setBounds( actions.reduced( 3 ) );

        auto bar = b.removeFromBottom( 32 );
        prevPage.setBounds( bar.removeFromLeft( 50 ).reduced( 3 ) );
        nextPage.setBounds( bar.removeFromRight( 50 ).reduced( 3 ) );
        pageLabel.setBounds( bar );

        hint.setBounds( b.reduced( 10 ) );
        const auto kw = b.getWidth() / 2, kh = b.getHeight() / 2;
        for( int k = 0; k < knobsPerPage; ++k )
            knobs[(size_t)k]->setBounds( juce::Rectangle<int>( b.getX() + ( k % 2 ) * kw, b.getY() + ( k / 2 ) * kh, kw, kh ).reduced( 4 ) );
    }
} // namespace kick_ui
