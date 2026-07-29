/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include <dialogs/dialog_scope_waveforms.h>

#include <base_units.h>
#include <bitmaps.h>
#include <sch_edit_frame.h>
#include <settings/color_settings.h>
#include <widgets/color_swatch.h>
#include <widgets/font_choice.h>
#include <widgets/grid_color_swatch_helpers.h>
#include <widgets/std_bitmap_button.h>
#include <widgets/unit_binder.h>
#include <widgets/wx_grid.h>

#include <algorithm>
#include <set>
#include <wx/bmpcbox.h>
#include <wx/button.h>
#include <wx/listbox.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/srchctrl.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>


namespace
{
enum GRID_COLUMNS
{
    COL_WAVEFORM,
    COL_COLOR,
    COL_WIDTH,
    COL_COUNT
};
}


DIALOG_SCOPE_WAVEFORMS::DIALOG_SCOPE_WAVEFORMS( SCH_EDIT_FRAME* aParent,
                                                const SCH_SCOPE::SETTINGS& aSettings,
                                                const std::vector<wxString>& aAvailable,
                                                const std::map<wxString, KIGFX::COLOR4D>& aSignalColors ) :
        DIALOG_SHIM( aParent, wxID_ANY, _( "Scope Waveforms" ), wxDefaultPosition,
                     wxSize( 880, 740 ) ),
        m_settings( aSettings ),
        m_available( aAvailable ),
        m_signalColors( aSignalColors )
{
    const KIGFX::COLOR4D canvasColor =
            aParent->GetColorSettings()->GetColor( LAYER_SCHEMATIC_BACKGROUND );
    const SCH_SCOPE::SETTINGS defaults = SCH_SCOPE::DefaultSettings();
    wxBoxSizer* mainSizer = new wxBoxSizer( wxVERTICAL );
    wxBoxSizer* listsSizer = new wxBoxSizer( wxHORIZONTAL );

    wxStaticBoxSizer* selectedSizer = new wxStaticBoxSizer( wxVERTICAL, this,
                                                            _( "Displayed waveforms" ) );
    m_selectedGrid = new WX_GRID( selectedSizer->GetStaticBox(), wxID_ANY );
    m_selectedGrid->CreateGrid( 0, COL_COUNT );
    m_selectedGrid->SetRowLabelSize( 0 );
    m_selectedGrid->SetColLabelValue( COL_WAVEFORM, _( "Waveform" ) );
    m_selectedGrid->SetColLabelValue( COL_COLOR, _( "Color" ) );
    m_selectedGrid->SetColLabelValue( COL_WIDTH, _( "Width (mm)" ) );
    m_selectedGrid->SetColSize( COL_WAVEFORM, 250 );
    m_selectedGrid->SetColSize( COL_COLOR, 76 );
    m_selectedGrid->SetColSize( COL_WIDTH, 95 );
    m_selectedGrid->SetSelectionMode( wxGrid::wxGridSelectRows );
    m_selectedGrid->EnableDragRowSize( false );
    m_selectedGrid->EnableDragColMove( false );
    m_selectedGrid->EnableAlternateRowColors();

    wxGridCellAttr* waveformAttr = new wxGridCellAttr;
    waveformAttr->SetReadOnly();
    m_selectedGrid->SetColAttr( COL_WAVEFORM, waveformAttr );

    wxGridCellAttr* colorAttr = new wxGridCellAttr;
    colorAttr->SetRenderer( new GRID_CELL_COLOR_RENDERER( this ) );
    colorAttr->SetEditor( new GRID_CELL_COLOR_SELECTOR( this, m_selectedGrid ) );
    colorAttr->SetAlignment( wxALIGN_CENTER, wxALIGN_CENTER );
    m_selectedGrid->SetColAttr( COL_COLOR, colorAttr );

    wxGridCellAttr* widthAttr = new wxGridCellAttr;
    widthAttr->SetEditor( new wxGridCellFloatEditor( 0, 3 ) );
    widthAttr->SetAlignment( wxALIGN_RIGHT, wxALIGN_CENTER );
    m_selectedGrid->SetColAttr( COL_WIDTH, widthAttr );
    wxBoxSizer* selectedContent = new wxBoxSizer( wxHORIZONTAL );
    selectedContent->Add( m_selectedGrid, 1, wxEXPAND | wxALL, 6 );
    wxBoxSizer* orderButtons = new wxBoxSizer( wxVERTICAL );
    orderButtons->AddStretchSpacer();
    m_moveUpButton = new STD_BITMAP_BUTTON( selectedSizer->GetStaticBox(), wxID_ANY,
                                             wxNullBitmap );
    m_moveUpButton->SetBitmap( KiBitmapBundle( BITMAPS::small_up ) );
    m_moveUpButton->SetToolTip( _( "Move waveform up" ) );
    orderButtons->Add( m_moveUpButton, 0, wxALL, 3 );
    m_moveDownButton = new STD_BITMAP_BUTTON( selectedSizer->GetStaticBox(), wxID_ANY,
                                               wxNullBitmap );
    m_moveDownButton->SetBitmap( KiBitmapBundle( BITMAPS::small_down ) );
    m_moveDownButton->SetToolTip( _( "Move waveform down" ) );
    orderButtons->Add( m_moveDownButton, 0, wxALL, 3 );
    orderButtons->AddStretchSpacer();
    selectedContent->Add( orderButtons, 0, wxEXPAND | wxRIGHT, 3 );
    selectedSizer->Add( selectedContent, 1, wxEXPAND );
    listsSizer->Add( selectedSizer, 1, wxEXPAND );

    wxBoxSizer* buttonSizer = new wxBoxSizer( wxVERTICAL );
    buttonSizer->AddStretchSpacer();
    m_addButton = new STD_BITMAP_BUTTON( this, wxID_ANY, wxNullBitmap );
    m_addButton->SetBitmap( KiBitmapBundle( BITMAPS::left ) );
    m_addButton->SetToolTip( _( "Add selected waveforms to this scope" ) );
    buttonSizer->Add( m_addButton, 0, wxALL, 4 );
    m_removeButton = new STD_BITMAP_BUTTON( this, wxID_ANY, wxNullBitmap );
    m_removeButton->SetBitmap( KiBitmapBundle( BITMAPS::right ) );
    m_removeButton->SetToolTip( _( "Remove selected waveforms from this scope" ) );
    buttonSizer->Add( m_removeButton, 0, wxALL, 4 );
    buttonSizer->AddStretchSpacer();
    listsSizer->Add( buttonSizer, 0, wxEXPAND | wxLEFT | wxRIGHT, 4 );

    wxStaticBoxSizer* availableSizer = new wxStaticBoxSizer( wxVERTICAL, this,
                                                             _( "Available waveforms" ) );
    m_filter = new wxSearchCtrl( availableSizer->GetStaticBox(), wxID_ANY );
    m_filter->SetDescriptiveText( _( "Filter waveforms" ) );
    m_filter->ShowCancelButton( true );
    availableSizer->Add( m_filter, 0, wxEXPAND | wxALL, 6 );
    m_availableList = new wxListBox( availableSizer->GetStaticBox(), wxID_ANY,
                                     wxDefaultPosition, wxDefaultSize, 0, nullptr,
                                     wxLB_EXTENDED | wxLB_NEEDED_SB );
    availableSizer->Add( m_availableList, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6 );
    m_emptyLabel = new wxStaticText( availableSizer->GetStaticBox(), wxID_ANY,
                                     _( "No waveform sources are available for the current simulation." ) );
    availableSizer->Add( m_emptyLabel, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6 );
    listsSizer->Add( availableSizer, 1, wxEXPAND );
    mainSizer->Add( listsSizer, 1, wxEXPAND | wxALL, 10 );

    wxStaticBoxSizer* appearanceSizer = new wxStaticBoxSizer( wxVERTICAL, this,
                                                              _( "Appearance" ) );
    wxFlexGridSizer* appearanceGrid = new wxFlexGridSizer( 0, 6, 6, 8 );
    appearanceGrid->AddGrowableCol( 1 );

    appearanceGrid->Add( new wxStaticText( appearanceSizer->GetStaticBox(), wxID_ANY,
                                           _( "Background:" ) ),
                         0, wxALIGN_CENTER_VERTICAL );
    m_backgroundSwatch = new COLOR_SWATCH( appearanceSizer->GetStaticBox(),
                                            m_settings.backgroundColor, wxID_ANY, canvasColor,
                                            defaults.backgroundColor, SWATCH_MEDIUM, true );
    appearanceGrid->Add( m_backgroundSwatch, 0, wxALIGN_CENTER_VERTICAL );
    appearanceGrid->AddSpacer( 1 );
    appearanceGrid->AddSpacer( 1 );
    appearanceGrid->AddSpacer( 1 );
    appearanceGrid->AddSpacer( 1 );

    appearanceGrid->Add( new wxStaticText( appearanceSizer->GetStaticBox(), wxID_ANY,
                                           _( "Border color:" ) ),
                         0, wxALIGN_CENTER_VERTICAL );
    m_borderSwatch = new COLOR_SWATCH( appearanceSizer->GetStaticBox(), m_settings.borderColor,
                                       wxID_ANY, canvasColor, defaults.borderColor,
                                       SWATCH_MEDIUM, true );
    appearanceGrid->Add( m_borderSwatch, 0, wxALIGN_CENTER_VERTICAL );
    wxStaticText* borderWidthLabel = new wxStaticText( appearanceSizer->GetStaticBox(), wxID_ANY,
                                                       _( "Width:" ) );
    appearanceGrid->Add( borderWidthLabel, 0, wxALIGN_CENTER_VERTICAL );
    m_borderWidthCtrl = new wxTextCtrl( appearanceSizer->GetStaticBox(), wxID_ANY );
    appearanceGrid->Add( m_borderWidthCtrl, 0, wxALIGN_CENTER_VERTICAL | wxEXPAND );
    wxStaticText* borderUnits = new wxStaticText( appearanceSizer->GetStaticBox(), wxID_ANY,
                                                  wxEmptyString );
    appearanceGrid->Add( borderUnits, 0, wxALIGN_CENTER_VERTICAL );
    appearanceGrid->AddSpacer( 1 );

    appearanceGrid->Add( new wxStaticText( appearanceSizer->GetStaticBox(), wxID_ANY,
                                           _( "Grid:" ) ),
                         0, wxALIGN_CENTER_VERTICAL );
    m_gridStyle = new wxBitmapComboBox( appearanceSizer->GetStaticBox(), wxID_ANY );
    m_gridStyle->Append( _( "Off" ) );

    for( const auto& [lineStyle, lineStyleDesc] : lineTypeNames )
        m_gridStyle->Append( lineStyleDesc.name, KiBitmapBundle( lineStyleDesc.bitmap ) );

    m_gridStyle->SetSelection( m_settings.gridVisible
                                      ? 1 + static_cast<int>( m_settings.gridStyle )
                                      : 0 );
    appearanceGrid->Add( m_gridStyle, 1, wxALIGN_CENTER_VERTICAL | wxEXPAND );
    appearanceGrid->Add( new wxStaticText( appearanceSizer->GetStaticBox(), wxID_ANY,
                                           _( "Color:" ) ),
                         0, wxALIGN_CENTER_VERTICAL );
    m_gridSwatch = new COLOR_SWATCH( appearanceSizer->GetStaticBox(), m_settings.gridColor,
                                     wxID_ANY, canvasColor, defaults.gridColor,
                                     SWATCH_MEDIUM, true );
    appearanceGrid->Add( m_gridSwatch, 0, wxALIGN_CENTER_VERTICAL );
    wxStaticText* gridWidthLabel = new wxStaticText( appearanceSizer->GetStaticBox(), wxID_ANY,
                                                     _( "Width:" ) );
    appearanceGrid->Add( gridWidthLabel, 0, wxALIGN_CENTER_VERTICAL );
    wxBoxSizer* gridWidthSizer = new wxBoxSizer( wxHORIZONTAL );
    m_gridWidthCtrl = new wxTextCtrl( appearanceSizer->GetStaticBox(), wxID_ANY );
    gridWidthSizer->Add( m_gridWidthCtrl, 1 );
    wxStaticText* gridUnits = new wxStaticText( appearanceSizer->GetStaticBox(), wxID_ANY,
                                                wxEmptyString );
    gridWidthSizer->Add( gridUnits, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 4 );
    appearanceGrid->Add( gridWidthSizer, 0, wxALIGN_CENTER_VERTICAL | wxEXPAND );

    appearanceGrid->Add( new wxStaticText( appearanceSizer->GetStaticBox(), wxID_ANY,
                                           _( "Minor grid:" ) ),
                         0, wxALIGN_CENTER_VERTICAL );
    m_minorGridStyle = new wxBitmapComboBox( appearanceSizer->GetStaticBox(), wxID_ANY );
    m_minorGridStyle->Append( _( "Off" ) );

    for( const auto& [lineStyle, lineStyleDesc] : lineTypeNames )
        m_minorGridStyle->Append( lineStyleDesc.name, KiBitmapBundle( lineStyleDesc.bitmap ) );

    m_minorGridStyle->SetSelection( m_settings.minorGridVisible
                                            ? 1 + static_cast<int>( m_settings.minorGridStyle )
                                            : 0 );
    appearanceGrid->Add( m_minorGridStyle, 1, wxALIGN_CENTER_VERTICAL | wxEXPAND );
    appearanceGrid->Add( new wxStaticText( appearanceSizer->GetStaticBox(), wxID_ANY,
                                           _( "Color:" ) ),
                         0, wxALIGN_CENTER_VERTICAL );
    m_minorGridSwatch = new COLOR_SWATCH( appearanceSizer->GetStaticBox(),
                                          m_settings.minorGridColor, wxID_ANY, canvasColor,
                                          defaults.minorGridColor, SWATCH_MEDIUM, true );
    appearanceGrid->Add( m_minorGridSwatch, 0, wxALIGN_CENTER_VERTICAL );
    wxStaticText* minorGridWidthLabel = new wxStaticText( appearanceSizer->GetStaticBox(),
                                                           wxID_ANY, _( "Width:" ) );
    appearanceGrid->Add( minorGridWidthLabel, 0, wxALIGN_CENTER_VERTICAL );
    wxBoxSizer* minorGridWidthSizer = new wxBoxSizer( wxHORIZONTAL );
    m_minorGridWidthCtrl = new wxTextCtrl( appearanceSizer->GetStaticBox(), wxID_ANY );
    minorGridWidthSizer->Add( m_minorGridWidthCtrl, 1 );
    wxStaticText* minorGridUnits = new wxStaticText( appearanceSizer->GetStaticBox(), wxID_ANY,
                                                     wxEmptyString );
    minorGridWidthSizer->Add( minorGridUnits, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 4 );
    appearanceGrid->Add( minorGridWidthSizer, 0, wxALIGN_CENTER_VERTICAL | wxEXPAND );

    appearanceGrid->Add( new wxStaticText( appearanceSizer->GetStaticBox(), wxID_ANY,
                                           _( "Axis font:" ) ),
                         0, wxALIGN_CENTER_VERTICAL );
    wxString fontChoices[2];
    m_axisFont = new FONT_CHOICE( appearanceSizer->GetStaticBox(), wxID_ANY, wxDefaultPosition,
                                  wxDefaultSize, 2, fontChoices, 0 );
    m_axisFont->SetFontSelection( KIFONT::FONT::GetFont( m_settings.axisFontName ), true );
    appearanceGrid->Add( m_axisFont, 1, wxALIGN_CENTER_VERTICAL | wxEXPAND );
    wxStaticText* axisTextSizeLabel = new wxStaticText( appearanceSizer->GetStaticBox(), wxID_ANY,
                                                        _( "Text size:" ) );
    appearanceGrid->Add( axisTextSizeLabel, 0, wxALIGN_CENTER_VERTICAL );
    m_axisTextSizeCtrl = new wxTextCtrl( appearanceSizer->GetStaticBox(), wxID_ANY );
    appearanceGrid->Add( m_axisTextSizeCtrl, 0, wxALIGN_CENTER_VERTICAL | wxEXPAND );
    wxStaticText* axisTextSizeUnits = new wxStaticText( appearanceSizer->GetStaticBox(), wxID_ANY,
                                                        wxEmptyString );
    appearanceGrid->Add( axisTextSizeUnits, 0, wxALIGN_CENTER_VERTICAL );
    appearanceGrid->AddSpacer( 1 );

    appearanceSizer->Add( appearanceGrid, 1, wxEXPAND | wxALL, 8 );
    mainSizer->Add( appearanceSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10 );

    m_borderWidth = std::make_unique<UNIT_BINDER>( aParent, borderWidthLabel, m_borderWidthCtrl,
                                                   borderUnits );
    m_gridWidth = std::make_unique<UNIT_BINDER>( aParent, gridWidthLabel, m_gridWidthCtrl,
                                                 gridUnits );
    m_minorGridWidth = std::make_unique<UNIT_BINDER>( aParent, minorGridWidthLabel,
                                                       m_minorGridWidthCtrl, minorGridUnits );
    m_axisTextSize = std::make_unique<UNIT_BINDER>( aParent, axisTextSizeLabel,
                                                    m_axisTextSizeCtrl, axisTextSizeUnits );
    m_borderWidth->SetValue( m_settings.borderWidth );
    m_gridWidth->SetValue( m_settings.gridWidth );
    m_minorGridWidth->SetValue( m_settings.minorGridWidth );
    m_axisTextSize->SetValue( m_settings.axisTextSize );

    wxStdDialogButtonSizer* standardButtons = new wxStdDialogButtonSizer();
    standardButtons->AddButton( new wxButton( this, wxID_OK ) );
    standardButtons->AddButton( new wxButton( this, wxID_CANCEL ) );
    standardButtons->Realize();
    mainSizer->Add( standardButtons, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10 );

    SetSizer( mainSizer );
    SetMinSize( wxSize( 700, 530 ) );

    m_addButton->Bind( wxEVT_BUTTON, &DIALOG_SCOPE_WAVEFORMS::onAdd, this );
    m_removeButton->Bind( wxEVT_BUTTON, &DIALOG_SCOPE_WAVEFORMS::onRemove, this );
    m_moveUpButton->Bind( wxEVT_BUTTON, &DIALOG_SCOPE_WAVEFORMS::onMoveUp, this );
    m_moveDownButton->Bind( wxEVT_BUTTON, &DIALOG_SCOPE_WAVEFORMS::onMoveDown, this );
    m_filter->Bind( wxEVT_TEXT, &DIALOG_SCOPE_WAVEFORMS::onFilter, this );
    m_availableList->Bind( wxEVT_LISTBOX, &DIALOG_SCOPE_WAVEFORMS::onSelectionChanged, this );
    m_availableList->Bind( wxEVT_LISTBOX_DCLICK, &DIALOG_SCOPE_WAVEFORMS::onAdd, this );
    m_gridStyle->Bind( wxEVT_COMBOBOX, &DIALOG_SCOPE_WAVEFORMS::onGridStyleChanged, this );
    m_minorGridStyle->Bind( wxEVT_COMBOBOX,
                            &DIALOG_SCOPE_WAVEFORMS::onMinorGridStyleChanged, this );

    rebuildLists();
    updateGridControls();
    SetInitialFocus( m_filter );
    finishDialogSettings();
}


bool DIALOG_SCOPE_WAVEFORMS::TransferDataFromWindow()
{
    if( !m_selectedGrid->CommitPendingChanges() || !syncSourcesFromGrid( true ) )
        return false;

    if( !m_borderWidth->Validate( 0.01, 10.0, EDA_UNITS::MM )
        || !m_gridWidth->Validate( 0.01, 10.0, EDA_UNITS::MM )
        || !m_minorGridWidth->Validate( 0.01, 10.0, EDA_UNITS::MM )
        || !m_axisTextSize->Validate( 0.25, 10.0, EDA_UNITS::MM ) )
    {
        return false;
    }

    m_settings.backgroundColor = m_backgroundSwatch->GetSwatchColor();
    m_settings.borderColor = m_borderSwatch->GetSwatchColor();
    m_settings.borderWidth = m_borderWidth->GetIntValue();
    m_settings.gridVisible = m_gridStyle->GetSelection() > 0;
    m_settings.gridStyle = m_settings.gridVisible
                                   ? static_cast<LINE_STYLE>( m_gridStyle->GetSelection() - 1 )
                                   : LINE_STYLE::SOLID;
    m_settings.gridColor = m_gridSwatch->GetSwatchColor();
    m_settings.gridWidth = m_gridWidth->GetIntValue();
    m_settings.minorGridVisible = m_minorGridStyle->GetSelection() > 0;
    m_settings.minorGridStyle = m_settings.minorGridVisible
                                        ? static_cast<LINE_STYLE>(
                                                  m_minorGridStyle->GetSelection() - 1 )
                                        : LINE_STYLE::SOLID;
    m_settings.minorGridColor = m_minorGridSwatch->GetSwatchColor();
    m_settings.minorGridWidth = m_minorGridWidth->GetIntValue();
    KIFONT::FONT* axisFont = m_axisFont->GetFontSelection( false, false );
    m_settings.axisFontName = axisFont ? axisFont->GetName() : wxString();
    m_settings.axisTextSize = m_axisTextSize->GetIntValue();
    return DIALOG_SHIM::TransferDataFromWindow();
}


void DIALOG_SCOPE_WAVEFORMS::addSelectedWaveforms()
{
    syncSourcesFromGrid( false );
    wxArrayInt selections;
    m_availableList->GetSelections( selections );

    for( int index : selections )
    {
        const wxString value = m_availableList->GetString( static_cast<unsigned int>( index ) );
        bool exists = std::any_of( m_settings.sources.begin(), m_settings.sources.end(),
                                   [&]( const SCH_SCOPE::WAVEFORM_SOURCE& aSource )
                                   {
                                       return aSource.name == value;
                                   } );

        if( !exists )
        {
            auto color = m_signalColors.find( value );
            const KIGFX::COLOR4D defaultColor =
                    color != m_signalColors.end()
                            ? color->second
                            : SCH_SCOPE::DefaultWaveformColor( m_settings.sources.size() );
            m_settings.sources.push_back( { value, defaultColor,
                                            schIUScale.mmToIU( 0.2 ) } );
        }
    }

    rebuildLists();
}


void DIALOG_SCOPE_WAVEFORMS::removeSelectedWaveforms()
{
    syncSourcesFromGrid( false );
    wxArrayInt selectedRows = m_selectedGrid->GetSelectedRows();
    std::set<int, std::greater<int>> rows;

    for( int row : selectedRows )
        rows.insert( row );

    if( rows.empty() && m_selectedGrid->GetGridCursorRow() >= 0 )
        rows.insert( m_selectedGrid->GetGridCursorRow() );

    for( int row : rows )
    {
        if( row >= 0 && static_cast<size_t>( row ) < m_settings.sources.size() )
            m_settings.sources.erase( m_settings.sources.begin() + row );
    }

    rebuildLists();
}


void DIALOG_SCOPE_WAVEFORMS::moveSelectedWaveform( int aDelta )
{
    if( !syncSourcesFromGrid( false ) || m_settings.sources.size() < 2 )
        return;

    int row = m_selectedGrid->GetGridCursorRow();
    wxArrayInt selectedRows = m_selectedGrid->GetSelectedRows();

    if( !selectedRows.empty() )
        row = selectedRows.front();

    const int destination = row + aDelta;

    if( row < 0 || destination < 0
        || destination >= static_cast<int>( m_settings.sources.size() ) )
    {
        return;
    }

    std::swap( m_settings.sources[row], m_settings.sources[destination] );
    rebuildLists();
    m_selectedGrid->SetGridCursor( destination, COL_WAVEFORM );
    m_selectedGrid->SelectRow( destination );
}


void DIALOG_SCOPE_WAVEFORMS::rebuildLists()
{
    m_selectedGrid->Freeze();
    m_availableList->Freeze();

    if( m_selectedGrid->GetNumberRows() > 0 )
        m_selectedGrid->DeleteRows( 0, m_selectedGrid->GetNumberRows() );

    if( !m_settings.sources.empty() )
        m_selectedGrid->AppendRows( static_cast<int>( m_settings.sources.size() ) );

    for( size_t row = 0; row < m_settings.sources.size(); ++row )
    {
        const SCH_SCOPE::WAVEFORM_SOURCE& source = m_settings.sources[row];
        m_selectedGrid->SetCellValue( static_cast<int>( row ), COL_WAVEFORM, source.name );
        m_selectedGrid->SetCellValue( static_cast<int>( row ), COL_COLOR,
                                      source.color.ToCSSString() );
        m_selectedGrid->SetCellValue( static_cast<int>( row ), COL_WIDTH,
                                      wxString::Format( wxS( "%.3f" ),
                                                        schIUScale.IUTomm( source.lineWidth ) ) );
    }

    m_availableList->Clear();
    const wxString filter = m_filter->GetValue().Lower();

    for( const wxString& value : m_available )
    {
        bool selected = std::any_of( m_settings.sources.begin(), m_settings.sources.end(),
                                     [&]( const SCH_SCOPE::WAVEFORM_SOURCE& aSource )
                                     {
                                         return aSource.name == value;
                                     } );

        if( !selected && ( filter.IsEmpty() || value.Lower().Contains( filter ) ) )
            m_availableList->Append( value );
    }

    m_selectedGrid->Thaw();
    m_availableList->Thaw();
    m_emptyLabel->Show( m_available.empty() );
    Layout();
    updateButtons();
}


bool DIALOG_SCOPE_WAVEFORMS::syncSourcesFromGrid( bool aShowErrors )
{
    if( !m_selectedGrid->CommitPendingChanges( !aShowErrors ) )
        return false;

    for( int row = 0; row < m_selectedGrid->GetNumberRows(); ++row )
    {
        double widthMm = 0.0;
        const wxString widthText = m_selectedGrid->GetCellValue( row, COL_WIDTH );

        if( !widthText.ToDouble( &widthMm ) || widthMm < 0.01 || widthMm > 10.0 )
        {
            if( aShowErrors )
            {
                wxMessageBox( _( "Waveform width must be between 0.01 mm and 10 mm." ),
                              _( "Invalid Waveform Width" ), wxOK | wxICON_ERROR, this );
                m_selectedGrid->SetGridCursor( row, COL_WIDTH );
                m_selectedGrid->EnableCellEditControl();
            }

            return false;
        }

        if( static_cast<size_t>( row ) >= m_settings.sources.size() )
            break;

        m_settings.sources[row].color = KIGFX::COLOR4D(
                m_selectedGrid->GetCellValue( row, COL_COLOR ) );
        m_settings.sources[row].lineWidth = schIUScale.mmToIU( widthMm );
    }

    return true;
}


void DIALOG_SCOPE_WAVEFORMS::updateButtons()
{
    wxArrayInt availableSelections;
    m_addButton->Enable( m_availableList->GetSelections( availableSelections ) > 0 );
    m_removeButton->Enable( !m_settings.sources.empty() );
    m_moveUpButton->Enable( m_settings.sources.size() > 1 );
    m_moveDownButton->Enable( m_settings.sources.size() > 1 );
}


void DIALOG_SCOPE_WAVEFORMS::updateGridControls()
{
    const bool enabled = m_gridStyle->GetSelection() > 0;
    m_gridSwatch->Enable( enabled );
    m_gridWidth->Enable( enabled );

    const bool minorEnabled = m_minorGridStyle->GetSelection() > 0;
    m_minorGridSwatch->Enable( minorEnabled );
    m_minorGridWidth->Enable( minorEnabled );
}


void DIALOG_SCOPE_WAVEFORMS::onAdd( wxCommandEvent& aEvent )
{
    (void) aEvent;
    addSelectedWaveforms();
}


void DIALOG_SCOPE_WAVEFORMS::onRemove( wxCommandEvent& aEvent )
{
    (void) aEvent;
    removeSelectedWaveforms();
}


void DIALOG_SCOPE_WAVEFORMS::onMoveUp( wxCommandEvent& aEvent )
{
    (void) aEvent;
    moveSelectedWaveform( -1 );
}


void DIALOG_SCOPE_WAVEFORMS::onMoveDown( wxCommandEvent& aEvent )
{
    (void) aEvent;
    moveSelectedWaveform( 1 );
}


void DIALOG_SCOPE_WAVEFORMS::onFilter( wxCommandEvent& aEvent )
{
    (void) aEvent;
    rebuildLists();
}


void DIALOG_SCOPE_WAVEFORMS::onSelectionChanged( wxCommandEvent& aEvent )
{
    (void) aEvent;
    updateButtons();
}


void DIALOG_SCOPE_WAVEFORMS::onGridStyleChanged( wxCommandEvent& aEvent )
{
    (void) aEvent;
    updateGridControls();
}


void DIALOG_SCOPE_WAVEFORMS::onMinorGridStyleChanged( wxCommandEvent& aEvent )
{
    (void) aEvent;
    updateGridControls();
}
