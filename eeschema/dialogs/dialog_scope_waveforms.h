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

#ifndef DIALOG_SCOPE_WAVEFORMS_H
#define DIALOG_SCOPE_WAVEFORMS_H

#include <dialog_shim.h>
#include <sch_scope.h>

#include <memory>
#include <map>
#include <vector>


class COLOR_SWATCH;
class FONT_CHOICE;
class SCH_EDIT_FRAME;
class STD_BITMAP_BUTTON;
class UNIT_BINDER;
class WX_GRID;
class wxBitmapComboBox;
class wxListBox;
class wxSearchCtrl;
class wxStaticText;
class wxTextCtrl;


class DIALOG_SCOPE_WAVEFORMS : public DIALOG_SHIM
{
public:
    DIALOG_SCOPE_WAVEFORMS( SCH_EDIT_FRAME* aParent, const SCH_SCOPE::SETTINGS& aSettings,
                            const std::vector<wxString>& aAvailable,
                            const std::map<wxString, KIGFX::COLOR4D>& aSignalColors );

    const SCH_SCOPE::SETTINGS& GetSettings() const { return m_settings; }

    bool TransferDataFromWindow() override;

private:
    void addSelectedWaveforms();
    void removeSelectedWaveforms();
    void moveSelectedWaveform( int aDelta );
    void rebuildLists();
    bool syncSourcesFromGrid( bool aShowErrors );
    void updateButtons();
    void updateGridControls();

    void onAdd( wxCommandEvent& aEvent );
    void onRemove( wxCommandEvent& aEvent );
    void onMoveUp( wxCommandEvent& aEvent );
    void onMoveDown( wxCommandEvent& aEvent );
    void onFilter( wxCommandEvent& aEvent );
    void onSelectionChanged( wxCommandEvent& aEvent );
    void onGridStyleChanged( wxCommandEvent& aEvent );
    void onMinorGridStyleChanged( wxCommandEvent& aEvent );

private:
    SCH_SCOPE::SETTINGS   m_settings;
    std::vector<wxString> m_available;
    std::map<wxString, KIGFX::COLOR4D> m_signalColors;

    WX_GRID*              m_selectedGrid;
    wxListBox*            m_availableList;
    wxSearchCtrl*         m_filter;
    wxStaticText*         m_emptyLabel;
    STD_BITMAP_BUTTON*    m_addButton;
    STD_BITMAP_BUTTON*    m_removeButton;
    STD_BITMAP_BUTTON*    m_moveUpButton;
    STD_BITMAP_BUTTON*    m_moveDownButton;
    COLOR_SWATCH*         m_backgroundSwatch;
    COLOR_SWATCH*         m_borderSwatch;
    COLOR_SWATCH*         m_gridSwatch;
    COLOR_SWATCH*         m_minorGridSwatch;
    wxBitmapComboBox*     m_gridStyle;
    wxBitmapComboBox*     m_minorGridStyle;
    FONT_CHOICE*          m_axisFont;
    wxTextCtrl*           m_borderWidthCtrl;
    wxTextCtrl*           m_gridWidthCtrl;
    wxTextCtrl*           m_minorGridWidthCtrl;
    wxTextCtrl*           m_axisTextSizeCtrl;
    std::unique_ptr<UNIT_BINDER> m_borderWidth;
    std::unique_ptr<UNIT_BINDER> m_gridWidth;
    std::unique_ptr<UNIT_BINDER> m_minorGridWidth;
    std::unique_ptr<UNIT_BINDER> m_axisTextSize;
};


#endif // DIALOG_SCOPE_WAVEFORMS_H
