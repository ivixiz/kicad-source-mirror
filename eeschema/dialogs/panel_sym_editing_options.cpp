/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2009 Wayne Stambaugh <stambaughw@verizon.net>
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <pgm_base.h>
#include <eda_base_frame.h>
#include <kiway.h>
#include <sch_edit_frame.h>
#include <widgets/ui_common.h>
#include <settings/settings_manager.h>
#include <symbol_editor/symbol_editor_settings.h>
#include "panel_sym_editing_options.h"


#define MIN_GRID 25

PANEL_SYM_EDITING_OPTIONS::PANEL_SYM_EDITING_OPTIONS( wxWindow* aWindow,
                                                      UNITS_PROVIDER* aUnitsProvider,
                                                      wxWindow* aEventSource ) :
        PANEL_SYM_EDITING_OPTIONS_BASE( aWindow ),
        m_lineWidth( aUnitsProvider, aEventSource, m_lineWidthLabel, m_lineWidthCtrl,
                     m_lineWidthUnits ),
        m_textSize( aUnitsProvider, aEventSource, m_textSizeLabel, m_textSizeCtrl,
                    m_textSizeUnits ),
        m_junctionSize( aUnitsProvider, aEventSource, m_junctionSizeLabel, m_junctionSizeCtrl,
                        m_junctionSizeUnits ),
        m_pinLength( aUnitsProvider, aEventSource, m_pinLengthLabel, m_pinLengthCtrl,
                     m_pinLengthUnits ),
        m_pinNameSize( aUnitsProvider, aEventSource ,m_pinNameSizeLabel, m_pinNameSizeCtrl,
                       m_pinNameSizeUnits ),
        m_pinNumberSize( aUnitsProvider, aEventSource, m_pinNumSizeLabel, m_pinNumSizeCtrl,
                         m_pinNumSizeUnits ),
        m_pinPitch( aUnitsProvider, aEventSource, m_pinPitchLabel, m_pinPitchCtrl, m_pinPitchUnits ),
        m_eventSource( aEventSource )
{
    // These defaults are persisted in IU so metric values do not round through whole mils.
    m_textSize.SetUnits( EDA_UNITS::MM );
    m_junctionSize.SetUnits( EDA_UNITS::MM );
    m_pinNameSize.SetUnits( EDA_UNITS::MM );
    m_pinNumberSize.SetUnits( EDA_UNITS::MM );

    m_widthHelpText->SetFont( KIUI::GetInfoFont( this ).Italic() );
}


void PANEL_SYM_EDITING_OPTIONS::loadSymEditorSettings( SYMBOL_EDITOR_SETTINGS* aCfg )
{
    m_lineWidth.SetValue( schIUScale.MilsToIU( aCfg->m_Defaults.line_width ) );
    m_textSize.SetValue( aCfg->GetDefaultTextSize() );
    m_junctionSize.SetValue( aCfg->GetDefaultJunctionSize() );
    m_pinLength.SetValue( schIUScale.MilsToIU( aCfg->m_Defaults.pin_length ) );
    m_pinNumberSize.SetValue( aCfg->GetDefaultPinNumberSize() );
    m_pinNameSize.SetValue( aCfg->GetDefaultPinNameSize() );
    m_pinPitch.SetValue( schIUScale.MilsToIU( aCfg->m_Repeat.pin_step ) );
    m_spinRepeatLabel->SetValue( aCfg->m_Repeat.label_delta );
    m_dragPinsWithEdges->SetValue( aCfg->m_dragPinsAlongWithEdges );
}


bool PANEL_SYM_EDITING_OPTIONS::TransferDataToWindow()
{
    loadSymEditorSettings( GetAppSettings<SYMBOL_EDITOR_SETTINGS>( "symbol_editor" ) );
    return true;
}


bool PANEL_SYM_EDITING_OPTIONS::TransferDataFromWindow()
{
    if( SYMBOL_EDITOR_SETTINGS* cfg = GetAppSettings<SYMBOL_EDITOR_SETTINGS>( "symbol_editor" ) )
    {
        const int previousJunctionSize = cfg->m_Defaults.junction_size_iu;

        cfg->m_Defaults.line_width = schIUScale.IUToMils( m_lineWidth.GetIntValue() );
        cfg->m_Defaults.text_size = schIUScale.IUToMils( m_textSize.GetIntValue() );
        cfg->m_Defaults.text_size_iu = m_textSize.GetIntValue();
        cfg->m_Defaults.junction_size_iu = m_junctionSize.GetIntValue();
        cfg->m_Defaults.pin_length = schIUScale.IUToMils( m_pinLength.GetIntValue() );
        cfg->m_Defaults.pin_num_size = schIUScale.IUToMils( m_pinNumberSize.GetIntValue() );
        cfg->m_Defaults.pin_num_size_iu = m_pinNumberSize.GetIntValue();
        cfg->m_Defaults.pin_name_size = schIUScale.IUToMils( m_pinNameSize.GetIntValue() );
        cfg->m_Defaults.pin_name_size_iu = m_pinNameSize.GetIntValue();
        cfg->m_Repeat.label_delta = m_spinRepeatLabel->GetValue();
        cfg->m_Repeat.pin_step = schIUScale.IUToMils( m_pinPitch.GetIntValue() );
        cfg->m_dragPinsAlongWithEdges = m_dragPinsWithEdges->GetValue();

        // Force pin_step to a grid multiple
        cfg->m_Repeat.pin_step = KiROUND( double( cfg->m_Repeat.pin_step ) / MIN_GRID ) * MIN_GRID;

        if( previousJunctionSize != cfg->m_Defaults.junction_size_iu )
            refreshSchematicJunctions();
    }

    return true;
}


void PANEL_SYM_EDITING_OPTIONS::refreshSchematicJunctions()
{
    EDA_BASE_FRAME* eventFrame = dynamic_cast<EDA_BASE_FRAME*>( m_eventSource );

    if( !eventFrame )
        return;

    SCH_EDIT_FRAME* schematicFrame = dynamic_cast<SCH_EDIT_FRAME*>( eventFrame );

    if( !schematicFrame )
        schematicFrame = dynamic_cast<SCH_EDIT_FRAME*>(
                eventFrame->Kiway().Player( FRAME_SCH, false ) );

    if( schematicFrame && schematicFrame->GetCanvas() )
    {
        // Junction bounds depend on the default.  Rebuild the cached view bounds once, rather
        // than touching every junction or changing the schematic file.
        schematicFrame->GetCanvas()->GetView()->UpdateAllItems( KIGFX::ALL );
        schematicFrame->GetCanvas()->ForceRefresh();
    }
}


void PANEL_SYM_EDITING_OPTIONS::onKillFocusPinPitch( wxFocusEvent& aEvent )
{
    int pitch_mils = schIUScale.IUToMils( m_pinPitch.GetIntValue() );

    // Force pin_step to a grid multiple
    pitch_mils = KiROUND( double( pitch_mils ) / MIN_GRID ) * MIN_GRID;

    m_pinPitch.SetValue( schIUScale.MilsToIU( pitch_mils ) );

    aEvent.Skip();
}


void PANEL_SYM_EDITING_OPTIONS::ResetPanel()
{
    SYMBOL_EDITOR_SETTINGS cfg;
    cfg.Load();                     // Loading without a file will init to defaults

    loadSymEditorSettings( &cfg );
}
