/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

namespace kick_ui
{
    namespace
    {
        namespace mm       = mt::modular;
        namespace PatchIDs = mm::PatchIDs;

        juce::Colour roleColour( mm::Role R )
        {
            switch( R )
            {
                case mm::Role::Source: return juce::Colour( 0xffc9821a );
                case mm::Role::Processor: return juce::Colour( 0xff2f78b0 );
                case mm::Role::Modulator: return juce::Colour( 0xff7552b8 );
                case mm::Role::Effect: return juce::Colour( 0xff23907f );
            }
            return juce::Colours::grey;
        }

        const juce::Colour cableColour{ 0xffe8a33a };
        const juce::Colour activeTab{ 0xff6b4a12 };

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

        juce::String formatAmount( double V ) { return ( V > 0 ? "+" : "" ) + juce::String( juce::roundToInt( V * 100.0 ) ) + "%"; }

        // The knob a module shows on the grid.
        juce::String summaryParam( const juce::String& Type )
        {
            static const std::map<juce::String, juce::String> key = {
                { "osc", "wave" },     { "noise", "color" },     { "filter", "cutoff" }, { "vca", "gain" },     { "adsr", "decay" },
                { "lfo", "rate" },     { "random", "polarity" }, { "math", "gain" },     { "drive", "drive" },  { "delay", "time" },
                { "crush", "bits" },   { "pan", "pan" },         { "fxdelay", "time" },  { "chorus", "rate" },  { "reverb", "size" },
                { "fxdrive", "drive" },
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

        // Module rows, a small gap, then the effect slots.
        float rowHeight() const { return (float)getHeight() / ( mm::gridRows + 1.3f ); }

        juce::Rectangle<float> cellBounds( int Col, int Row ) const
        {
            const auto ch = rowHeight();
            if( Row == effectRow )
            {
                const auto sw = (float)getWidth() / mm::effectSlots;
                return { Col * sw, (float)getHeight() - ch, sw, ch };
            }
            const auto cw = (float)getWidth() / mm::gridColumns;
            return { Col * cw, Row * ch, cw, ch };
        }

        bool cellAt( juce::Point<float> P, int& Col, int& Row ) const
        {
            const auto ch = rowHeight();
            if( P.y >= (float)getHeight() - ch )
            {
                Row = effectRow;
                Col = (int)std::floor( P.x / ( (float)getWidth() / mm::effectSlots ) );
                return juce::isPositiveAndBelow( Col, mm::effectSlots );
            }
            Col = (int)std::floor( P.x / ( (float)getWidth() / mm::gridColumns ) );
            Row = (int)std::floor( P.y / ch );
            return juce::isPositiveAndBelow( Col, mm::gridColumns ) && juce::isPositiveAndBelow( Row, mm::gridRows );
        }

        void paint( juce::Graphics& g ) override
        {
            const auto patch = editor.view();
            const auto read  = editor.reader();
            const auto empty = ui::Theme::backgroundColour( 1 ).brighter( 0.08f );

            for( int c = 0; c < mm::gridColumns; ++c )
                for( int r = 0; r < mm::gridRows; ++r )
                {
                    g.setColour( empty );
                    g.fillRoundedRectangle( cellBounds( c, r ).reduced( 3.f ), 4.f );
                }
            for( int slot = 0; slot < mm::effectSlots; ++slot )
            {
                const auto cell = cellBounds( slot, effectRow ).reduced( 3.f );
                g.setColour( empty.darker( 0.15f ) );
                g.fillRoundedRectangle( cell, 4.f );
                g.setColour( ui::Theme::textColour( 1 ).withAlpha( 0.35f ) );
                g.setFont( juce::Font( 12.f ) );
                g.drawText( "FX " + juce::String( slot + 1 ), cell.toNearestInt(), juce::Justification::centred );
            }

            // Signal flow: a line down each column that holds audio modules, into the effects.
            const auto fxTop = cellBounds( 0, effectRow ).getY();
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
                g.setColour( ui::Theme::textColour( 1 ).withAlpha( 0.45f ) );
                g.drawLine( x, y0, x, fxTop - 1.f, 2.f );
                juce::Path arrow;
                arrow.addTriangle( x - 6.f, fxTop - 9.f, x + 6.f, fxTop - 9.f, x, fxTop - 1.f );
                g.fillPath( arrow );
            }

            // Which modules relate to the selection through modulation or cables.
            const auto         selected       = editor.selectedModule();
            std::set<int>      related;
            const juce::String selectedSource = selected.isValid() ? mm::PatchBuilder::module( (int)selected.getProperty( PatchIDs::id ) ) : juce::String();
            for( const auto& m : patch )
                for( const auto& r : m )
                {
                    if( !r.hasType( PatchIDs::MOD ) && !r.hasType( PatchIDs::CABLE ) )
                        continue;
                    const auto source = r.getProperty( PatchIDs::source ).toString();
                    if( m == selected && source.startsWith( "module:" ) )
                        related.insert( source.fromFirstOccurrenceOf( ":", false, false ).getIntValue() ); // what feeds it
                    if( selectedSource.isNotEmpty() && source == selectedSource )
                        related.insert( (int)m.getProperty( PatchIDs::id ) ); // what it feeds
                }

            for( const auto& m : patch )
            {
                const auto* spec = mm::PatchEditor::specOf( m );
                if( !spec )
                    continue;
                const int id = m.getProperty( PatchIDs::id );
                if( dragging && id == dragId )
                    continue;
                drawModule( g, m, *spec, cellOf( m ).reduced( 3.f ), related.count( id ) > 0 );
            }

            drawCables( g, patch, read );

            g.setColour( juce::Colours::white );
            g.drawRoundedRectangle( cellBounds( editor.selCol, editor.selRow ).reduced( 2.f ), 5.f, 2.5f );

            // A module being dragged follows the finger.
            if( dragging )
                if( auto m = read.module( dragId ); m.isValid() )
                    if( const auto* spec = mm::PatchEditor::specOf( m ) )
                    {
                        g.setOpacity( 0.8f );
                        drawModule( g, m, *spec, cellBounds( 0, 0 ).reduced( 3.f ).withCentre( dragPos ), false );
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
            if( r != effectRow )
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
            int        c, r;
            const bool inside = cellAt( e.position, c, r );
            if( dragging )
            {
                dragging = false;
                if( inside && r != effectRow )
                    editor.moveModule( c, r );
                repaint();
            }
            else if( inside && wasSelected && c == editor.selCol && r == editor.selRow && !editor.selectedModule().isValid() )
                editor.pickType(); // tap an empty cell again to add a module (or an effect)
        }

    private:
        juce::Rectangle<float> cellOf( const juce::ValueTree& M ) const
        {
            if( M.hasType( PatchIDs::FX ) )
                return cellBounds( M.getProperty( PatchIDs::slot ), effectRow );
            return cellBounds( M.getProperty( PatchIDs::col ), M.getProperty( PatchIDs::row ) );
        }

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

            // ~N = modulated knobs, a dot = cables in.
            int mods = 0, cables = 0;
            for( const auto& r : M )
            {
                mods += r.hasType( PatchIDs::MOD ) ? 1 : 0;
                cables += r.hasType( PatchIDs::CABLE ) ? 1 : 0;
            }
            if( mods )
            {
                g.setColour( roleColour( mm::Role::Modulator ).brighter( 0.6f ) );
                g.setFont( juce::Font( 13.f, juce::Font::bold ) );
                g.drawText( "~" + juce::String( mods ), Cell.reduced( 4.f, 2.f ).toNearestInt(), juce::Justification::topRight, false );
            }
            if( cables )
            {
                g.setColour( cableColour );
                g.fillEllipse( Cell.getX() + 4.f, Cell.getY() + 4.f, 7.f, 7.f );
            }
        }

        void drawCables( juce::Graphics& g, const juce::ValueTree& Patch, const mm::PatchEditor& Read )
        {
            for( const auto& m : Patch )
                for( const auto& c : m )
                {
                    if( !c.hasType( PatchIDs::CABLE ) )
                        continue;
                    const auto from = Read.module( c.getProperty( PatchIDs::source ).toString().fromFirstOccurrenceOf( ":", false, false ).getIntValue() );
                    if( !from.isValid() )
                        continue;
                    const auto a = cellOf( from ).getCentre(), b = cellOf( m ).getCentre();
                    // Bow the cable sideways so it does not hide the column's flow line.
                    const auto mid    = ( a + b ) * 0.5f;
                    const auto normal = juce::Point<float>( b.y - a.y, a.x - b.x ) * 0.25f;
                    juce::Path path;
                    path.startNewSubPath( a );
                    path.quadraticTo( mid + ( normal.isOrigin() ? juce::Point<float>( 20.f, 0.f ) : normal ), b );
                    g.setColour( cableColour.withAlpha( 0.9f ) );
                    g.strokePath( path, juce::PathStrokeType( 2.5f ) );
                    g.fillEllipse( juce::Rectangle<float>( 9.f, 9.f ).withCentre( b ) );
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
        mt::utils::addAndMakeVisible( this, back, title, macros, save, voices, moduleTitle, hint, paramsTab, modsTab, inputsTab, addButton, deleteButton, linkButton, removeButton,
                                      prevPage, nextPage, pageLabel );

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
                if( mode != Mode::Params )
                {
                    selectedItem = page * knobsPerPage + (int)i;
                    refreshInspector();
                }
            };
            addAndMakeVisible( *k );
        }

        back.onClick         = [this]() { if( onClose ) onClose(); };
        voices.onClick       = [this]() { pickVoices(); };
        macros.onClick       = [this]() { pickMacro(); };
        save.onClick         = [this]() { savePreset(); };
        paramsTab.onClick    = [this]() { showMode( Mode::Params ); };
        modsTab.onClick      = [this]() { showMode( Mode::Mods ); };
        inputsTab.onClick    = [this]() { showMode( Mode::Inputs ); };
        addButton.onClick    = [this]() { pickType(); };
        deleteButton.onClick = [this]() { deleteModule(); };
        linkButton.onClick   = [this]() { pickLink(); };
        removeButton.onClick = [this]() {
            const auto items = selectedItems();
            if( juce::isPositiveAndBelow( selectedItem, (int)items.size() ) )
            {
                edit().removeMod( items[(size_t)selectedItem] );
                selectedItem = -1;
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
        selectedItem = -1;
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

    juce::ValueTree ModularEditor::selectedModule() const
    {
        return effectSelected() ? reader().effectAt( selCol ) : reader().moduleAt( selCol, selRow );
    }

    std::vector<juce::ValueTree> ModularEditor::selectedLinks( const juce::Identifier& Type ) const
    {
        std::vector<juce::ValueTree> links;
        for( const auto& r : selectedModule() )
            if( r.hasType( Type ) )
                links.push_back( r );
        return links;
    }

    std::vector<juce::ValueTree> ModularEditor::selectedItems() const
    {
        return selectedLinks( mode == Mode::Inputs ? PatchIDs::CABLE : PatchIDs::MOD );
    }

    //==============================================================================
    void ModularEditor::selectCell( int Col, int Row )
    {
        Row = juce::jlimit( 0, (int)effectRow, Row );
        Col = juce::jlimit( 0, ( Row == effectRow ? mm::effectSlots : mm::gridColumns ) - 1, Col );
        if( Col != selCol || Row != selRow )
        {
            selCol       = Col;
            selRow       = Row;
            page         = 0;
            selectedItem = -1;
        }
        refresh();
    }

    void ModularEditor::addModule( const juce::String& Type )
    {
        if( effectSelected() )
            edit().addEffect( Type, selCol );
        else
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
        if( auto m = selectedModule(); m.isValid() && !effectSelected() && Row != effectRow )
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
        mode         = Mode::Mods;
        selectedItem = (int)selectedItems().size() - 1; // the new one is last
        page         = juce::jmax( 0, selectedItem ) / knobsPerPage;
    }

    void ModularEditor::addCable( const juce::String& Source, float Amount )
    {
        auto m = selectedModule();
        if( !m.isValid() || effectSelected() )
            return;
        edit().addCable( m.getProperty( PatchIDs::id ), Source, Amount );
        mode         = Mode::Inputs;
        selectedItem = (int)selectedItems().size() - 1;
        page         = juce::jmax( 0, selectedItem ) / knobsPerPage;
    }

    void ModularEditor::showMode( Mode M )
    {
        mode         = M;
        page         = 0;
        selectedItem = -1;
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

    void ModularEditor::pickType()
    {
        if( selectedModule().isValid() || !program )
            return;
        juce::StringArray names, types;
        for( const auto& s : effectSelected() ? mm::getEffectSpecs() : mm::getModuleSpecs() )
        {
            names.add( s.name );
            types.add( s.type );
        }
        pick( effectSelected() ? "ADD EFFECT" : "ADD MODULE", names, [this, types]( int Index ) { addModule( types[Index] ); } );
    }

    void ModularEditor::pickLink()
    {
        const auto  m    = selectedModule();
        const auto* spec = mm::PatchEditor::specOf( m );
        if( !spec )
            return;
        const int id = m.getProperty( PatchIDs::id );

        if( mode == Mode::Inputs )
        {
            juce::StringArray ids, names;
            for( const auto& [source, name] : reader().getCableSources( id ) )
            {
                ids.add( source );
                names.add( name );
            }
            if( ids.isEmpty() )
            {
                mt::utils::showNotification( "Add another oscillator, noise or processor to cable from." );
                return;
            }
            pick( "CABLE AUDIO FROM", names, [this, ids]( int S ) { addCable( ids[S] ); } );
            return;
        }

        juce::StringArray params;
        for( const auto& p : spec->params )
            params.add( p.name );

        const auto self   = mm::PatchBuilder::module( id );
        const auto global = spec->role == mm::Role::Effect;
        pick( "MODULATE WHICH KNOB?", params, [this, spec, self, global]( int P ) {
            const auto        param = spec->params[(size_t)P].id;
            juce::StringArray ids, names;
            for( const auto& [source, name] : reader().getSources( global ) )
                if( source != self ) // a modulator cannot modulate itself
                {
                    ids.add( source );
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

    void ModularEditor::pickMacro()
    {
        if( !program )
            return;
        juce::StringArray options;
        for( int m = 0; m < 8; ++m )
            options.add( "Macro " + juce::String( m + 1 ) + ": " + reader().macroLabel( m ) );
        pick( "RENAME WHICH MACRO?", options, [this]( int M ) {
            juce::MessageManager::callAsync( [this, M]() {
                askName( "MACRO " + juce::String( M + 1 ) + " NAME (EMPTY = AUTOMATIC)", reader().macroLabel( M ),
                         [this, M]( const juce::String& Name ) { edit().setMacroName( M, Name ); } );
            } );
        } );
    }

    void ModularEditor::askName( const juce::String& Title, const juce::String& Initial, std::function<void( const juce::String& )> OnName )
    {
        auto& keyboard          = ui::KickVirtualKeyBoard::getWithText( Initial );
        keyboard.onEnterPressed = [&keyboard]( int ) { mt::utils::exitDialog( keyboard.getCurrentlyFocusedComponent(), UI::APPLY ); };
        keyboard.show(
            Title, ui::KickVirtualKeyBoard::Type::KEYS_LOWERCASE_ALPHABETIC,
            [&keyboard, OnName]( int R ) {
                if( R == UI::APPLY && OnName )
                    OnName( keyboard.getInputText().trim() );
            },
            ui::layout::keyBoardPopup );
    }

    void ModularEditor::savePreset()
    {
        if( !program )
            return;
        auto name = view().getProperty( PatchIDs::name ).toString();
        if( name.endsWith( " *" ) )
            name = name.dropLastCharacters( 2 );
        if( mm::getPresetNames().contains( name ) )
            name << " 2"; // a factory name cannot be reused
        askName( "SAVE PRESET AS", name, [this]( const juce::String& Name ) {
            if( auto* um = program->getUndoManager() )
                um->beginNewTransaction();
            if( program->savePreset( Name ) )
                mt::utils::showNotification( "Saved preset \"" + Name + "\"" );
            else
                mt::utils::showNotification( "Choose a name that is not empty or a factory preset's" );
        } );
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
        if( mode != Mode::Params )
            return (int)selectedItems().size();
        const auto* spec = mm::PatchEditor::specOf( selectedModule() );
        return spec ? (int)spec->params.size() : 0;
    }

    void ModularEditor::refreshInspector()
    {
        const auto  m     = selectedModule();
        const auto* spec  = mm::PatchEditor::specOf( m );
        const auto  empty = spec == nullptr;
        const auto  read  = reader();
        const auto  isFx  = effectSelected();

        // IN is for audio modules only.
        const bool takesCables = spec && ( spec->role == mm::Role::Source || spec->role == mm::Role::Processor );
        if( ( mode == Mode::Inputs && !takesCables ) || empty )
            mode = Mode::Params;

        if( empty )
        {
            moduleTitle.setText( isFx ? "Empty effect slot" : "Empty cell", juce::dontSendNotification );
            hint.setText( isFx ? "Tap + EFFECT, or tap the slot again, to add an effect.\n\n"
                                 "The voices are mixed, then run through the effects from FX 1 to FX 4. "
                                 "Macros, the mod wheel and pitch bend can modulate them."
                               : "Tap + MODULE, or tap the cell again, to add a module.\n\n"
                                 "Audio runs down each column and the columns are mixed. "
                                 "Envelopes, LFOs and Random can go anywhere: they modulate knobs. "
                                 "Cables (IN) bring audio from any other column.",
                          juce::dontSendNotification );
            addButton.setButtonText( isFx ? "+ EFFECT" : "+ MODULE" );
        }
        else
            moduleTitle.setText( read.moduleLabel( m.getProperty( PatchIDs::id ) ), juce::dontSendNotification );

        hint.setVisible( empty );
        addButton.setVisible( empty );
        for( auto* c : std::initializer_list<juce::Component*>{ &paramsTab, &modsTab, &deleteButton, &linkButton, &prevPage, &nextPage, &pageLabel } )
            c->setVisible( !empty );
        inputsTab.setVisible( takesCables );
        removeButton.setVisible( !empty && mode != Mode::Params );

        const auto mods   = selectedLinks( PatchIDs::MOD );
        const auto cables = selectedLinks( PatchIDs::CABLE );
        modsTab.setButtonText( "MODS (" + juce::String( (int)mods.size() ) + ")" );
        inputsTab.setButtonText( "IN (" + juce::String( (int)cables.size() ) + ")" );
        paramsTab.setBackgroundColour( mode == Mode::Params ? activeTab : ui::Theme::backgroundColour( 3 ) );
        modsTab.setBackgroundColour( mode == Mode::Mods ? activeTab : ui::Theme::backgroundColour( 3 ) );
        inputsTab.setBackgroundColour( mode == Mode::Inputs ? activeTab : ui::Theme::backgroundColour( 3 ) );
        linkButton.setButtonText( mode == Mode::Inputs ? "+ CABLE" : "+ MOD" );

        const auto pages = juce::jmax( 1, ( numItems() + knobsPerPage - 1 ) / knobsPerPage );
        page             = juce::jlimit( 0, pages - 1, page );
        const auto what  = juce::String( mode == Mode::Params ? "PARAMS " : mode == Mode::Mods ? "MODS " : "CABLES IN " );
        pageLabel.setText( what + juce::String( page + 1 ) + " / " + juce::String( pages ), juce::dontSendNotification );
        prevPage.setEnabled( page > 0 );
        nextPage.setEnabled( page + 1 < pages );

        // REMOVE acts on the modulation or cable last touched (its knob title starts with ">").
        const auto items        = selectedItems();
        const auto itemSelected = juce::isPositiveAndBelow( selectedItem, (int)items.size() );
        removeButton.setButtonText( itemSelected && selectedItem / knobsPerPage == page ? "REMOVE P" + juce::String( selectedItem % knobsPerPage + 1 ) : "REMOVE" );
        removeButton.setEnabled( itemSelected );

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
            const auto marker   = juce::String( index == selectedItem ? "> " : "" ) + "P" + juce::String( k + 1 ) + "  ";
            if( mode == Mode::Params )
            {
                const auto&                     p = spec->params[(size_t)index];
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
            else if( mode == Mode::Mods )
            {
                const auto& r     = items[(size_t)index];
                const auto  param = r.getProperty( PatchIDs::param ).toString();
                const auto  pi    = spec->findParam( param );
                const auto  name  = pi >= 0 ? spec->params[(size_t)pi].name.toUpperCase() : param;
                knob.setNormalisableRange( { -1.0, 1.0 } );
                knob.textFromValueFunction = formatAmount;
                knob.setTitle( marker + name + fromArrow + read.sourceLabel( r.getProperty( PatchIDs::source ) ).toUpperCase() );
                knob.setValue( (double)r.getProperty( PatchIDs::depth ), juce::dontSendNotification );
            }
            else
            {
                const auto& c = items[(size_t)index];
                knob.setNormalisableRange( { -1.0, 1.0 } );
                knob.textFromValueFunction = formatAmount;
                knob.setTitle( marker + "IN" + fromArrow + read.sourceLabel( c.getProperty( PatchIDs::source ) ).toUpperCase() );
                knob.setValue( (double)c.getProperty( PatchIDs::amount, 1.f ), juce::dontSendNotification );
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
            return;
        }

        const auto items = selectedItems();
        if( !juce::isPositiveAndBelow( index, (int)items.size() ) )
            return;
        selectedItem       = index;
        const auto gesture = juce::String( mode == Mode::Mods ? "depth:" : "amount:" ) + juce::String( id ) + ":" + juce::String( index );
        if( mode == Mode::Mods )
            edit( gesture ).setModDepth( items[(size_t)index], (float)Value );
        else
            edit( gesture ).setCableAmount( items[(size_t)index], (float)Value );
    }

    void ModularEditor::turnKnob( int Knob, int Steps )
    {
        if( Knob < 0 )
        {
            // The centre knob walks the cells row by row, then the effect slots.
            constexpr int cells = mm::gridColumns * mm::gridRows;
            const auto    now   = effectSelected() ? cells + selCol : selRow * mm::gridColumns + selCol;
            const auto    next  = juce::jlimit( 0, cells + mm::effectSlots - 1, now + Steps );
            if( next >= cells )
                selectCell( next - cells, effectRow );
            else
                selectCell( next % mm::gridColumns, next / mm::gridColumns );
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
        voices.setBounds( top.removeFromRight( 120 ).reduced( 3 ) );
        save.setBounds( top.removeFromRight( 90 ).reduced( 3 ) );
        macros.setBounds( top.removeFromRight( 110 ).reduced( 3 ) );
        title.setBounds( top.reduced( 12, 0 ) );
        b.removeFromTop( 6 );

        grid->setBounds( b.removeFromLeft( juce::roundToInt( b.getWidth() * 0.56f ) ) );
        b.removeFromLeft( 14 );

        auto       head   = b.removeFromTop( 36 );
        const auto tabs   = inputsTab.isVisible() ? 3 : 2;
        auto       tabRow = head.removeFromRight( 78 * tabs );
        paramsTab.setBounds( tabRow.removeFromLeft( 78 ).reduced( 2 ) );
        modsTab.setBounds( tabRow.removeFromLeft( 78 ).reduced( 2 ) );
        inputsTab.setBounds( tabRow.reduced( 2 ) );
        moduleTitle.setBounds( head );

        auto actions = b.removeFromBottom( 42 );
        addButton.setBounds( actions.reduced( 3 ) );
        const auto aw = actions.getWidth() / 3;
        deleteButton.setBounds( actions.removeFromLeft( aw ).reduced( 3 ) );
        linkButton.setBounds( actions.removeFromLeft( aw ).reduced( 3 ) );
        removeButton.setBounds( actions.reduced( 3 ) );

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
