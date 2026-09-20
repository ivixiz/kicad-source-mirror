/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 3
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

#include <unordered_set>

#include <wx/eventfilter.h>
#include <wx/grid.h>
#include <wx/module.h>
#include <wx/stc/stc.h>
#include <wx/textentry.h>
#include <wx/validate.h>
#include <wx/weakref.h>


namespace
{
wxChar closingDelimiter( int aChar )
{
    switch( aChar )
    {
    case '(': return ')';
    case '[': return ']';
    case '{': return '}';
    case '\'': return '\'';
    case '"': return '"';
    case '`': return '`';
    default: return 0;
    }
}


bool isClosingDelimiter( int aChar )
{
    return aChar == ')' || aChar == ']' || aChar == '}'
           || aChar == '\'' || aChar == '"' || aChar == '`';
}


wxGrid* gridForWindow( wxWindow* aWindow )
{
    if( auto* grid = dynamic_cast<wxGrid*>( aWindow ) )
        return grid;

    // Native key events originate in wxGrid's child canvas, not the grid itself.
    if( aWindow )
    {
        if( auto* grid = dynamic_cast<wxGrid*>( aWindow->GetParent() ) )
        {
            if( aWindow == grid->GetGridWindow() )
                return grid;
        }
    }

    return nullptr;
}
} // namespace


/**
 * Install delimiter pairing for text entries, including transient grid editors and Scintilla.
 *
 * The filter only discovers controls.  Editing happens in a normal character handler so the
 * control's validator can reject the keystroke first.  wxModule owns registration and cleanup
 * for all KiCad applications without requiring changes to individual dialogs or wxWidgets.
 */
class TEXT_ENTRY_AUTOPAIR : public wxModule, public wxEventFilter
{
public:
    bool OnInit() override
    {
        wxEvtHandler::AddFilter( this );
        return true;
    }

    void OnExit() override
    {
        wxEvtHandler::RemoveFilter( this );

        for( wxWindow* window : m_windows )
        {
            window->Unbind( wxEVT_CHAR, &TEXT_ENTRY_AUTOPAIR::onChar, this );
            window->Unbind( wxEVT_DESTROY, &TEXT_ENTRY_AUTOPAIR::onDestroy, this );
        }

        m_windows.clear();
    }

    int FilterEvent( wxEvent& aEvent ) override
    {
        if( aEvent.GetEventType() != wxEVT_CHAR )
            return Event_Skip;

        int key = static_cast<wxKeyEvent&>( aEvent ).GetUnicodeKey();

        if( !closingDelimiter( key ) && !isClosingDelimiter( key ) )
            return Event_Skip;

        wxWindow* window = dynamic_cast<wxWindow*>( aEvent.GetEventObject() );

        if( window && ( dynamic_cast<wxTextEntryBase*>( window ) || gridForWindow( window ) )
            && m_windows.insert( window ).second )
        {
            window->Bind( wxEVT_CHAR, &TEXT_ENTRY_AUTOPAIR::onChar, this );
            window->Bind( wxEVT_DESTROY, &TEXT_ENTRY_AUTOPAIR::onDestroy, this );
        }

        return Event_Skip;
    }

private:
    void onDestroy( wxWindowDestroyEvent& aEvent )
    {
        m_windows.erase( aEvent.GetWindow() );
        aEvent.Skip();
    }

    void onChar( wxKeyEvent& aEvent )
    {
        aEvent.Skip();

        int    key = aEvent.GetUnicodeKey();
        wxChar closing = closingDelimiter( key );

        // Ctrl+Alt may produce a printable delimiter through AltGr on non-US keyboards.
        if( ( !closing && !isClosingDelimiter( key ) ) || aEvent.MetaDown()
            || ( aEvent.ControlDown() && !aEvent.AltDown() ) )
        {
            return;
        }

        auto* window = static_cast<wxWindow*>( aEvent.GetEventObject() );

        // wxGrid's StartingKey() normally writes the first character directly.  Route that
        // keystroke through the newly opened editor so it gets the same validation and pairing.
        if( wxGrid* grid = gridForWindow( window ) )
        {
            if( !closing || !grid->CanEnableCellControl() || grid->IsCellEditControlEnabled() )
                return;

            grid->EnableCellEditControl();

            if( !grid->IsCellEditControlEnabled() )
                return;

            wxGridCellEditorPtr editor( grid->GetCellEditor( grid->GetGridCursorRow(),
                                                            grid->GetGridCursorCol() ) );
            wxWindow* control = editor->GetControl();

            if( control && dynamic_cast<wxTextEntryBase*>( control ) )
            {
                wxKeyEvent input( wxEVT_CHAR );
                input.SetEventObject( control );
                input.m_keyCode = key;
                input.m_uniChar = key;

                if( control->ProcessWindowEvent( input ) )
                    aEvent.Skip( false );
            }

            return;
        }

        auto* entry = dynamic_cast<wxTextEntryBase*>( window );
        auto* stc = dynamic_cast<wxStyledTextCtrl*>( window );

        if( !entry || !window->IsEnabled() || !entry->IsEditable()
            || ( !stc && window->HasFlag( wxTE_PASSWORD ) ) )
        {
            return;
        }

        if( stc && ( stc->GetSelections() != 1 || stc->SelectionIsRectangle() ) )
            return;

        // Scintilla positions are UTF-8 byte offsets; wxTextEntryBase::GetRange() instead
        // indexes a wxString.  Use the editor's own range API to preserve non-ASCII text.
        auto range = [&]( long aFrom, long aTo )
        {
            return stc ? stc->GetTextRange( aFrom, aTo ) : entry->GetRange( aFrom, aTo );
        };

        auto charAt = [&]( long aPosition ) -> int
        {
            if( aPosition < 0 || aPosition >= entry->GetLastPosition() )
                return 0;

            // All delimiters are ASCII.  Do not decode a partial UTF-8 character in Scintilla.
            if( stc )
                return stc->GetCharAt( aPosition );

            wxString text = entry->GetRange( aPosition, aPosition + 1 );
            return text.empty() ? 0 : text[0].GetValue();
        };

        long start, end;
        entry->GetSelection( &start, &end );

        long escapeStart = start;

        while( escapeStart > 0 && charAt( escapeStart - 1 ) == '\\' )
            --escapeStart;

        if( ( start - escapeStart ) % 2 != 0 )
            return;

        if( start == end && isClosingDelimiter( key ) && charAt( end ) == key )
        {
            // Accept any pending suggestion before stepping over the existing delimiter.
            if( stc && stc->AutoCompActive() )
            {
                stc->AutoCompComplete();
                entry->GetSelection( &start, &end );
            }

            if( start == end && charAt( end ) == key )
            {
                entry->SetSelection( end + 1, end + 1 );
                aEvent.Skip( false );
            }

            return;
        }

        if( !closing )
            return;

        // The opening character has already passed validation.  Check the generated one too,
        // since WriteText() intentionally bypasses character validators.
        if( wxValidator* validator = window->GetValidator() )
        {
            wxKeyEvent closingEvent( aEvent );
            closingEvent.m_keyCode = closing;
            closingEvent.m_uniChar = closing;
            closingEvent.Skip( false );

            if( validator->ProcessEventLocally( closingEvent ) )
                return;
        }

        wxString replacement = wxString( wxUniChar( key ) ) + range( start, end ) + closing;
        wxWeakRef<wxWindow> alive( window );

        if( stc )
            stc->BeginUndoAction();

        entry->WriteText( replacement );
        aEvent.Skip( false );

        // Text-change handlers may destroy a transient editor.
        if( !alive )
            return;

        entry->SetSelection( start + 1, end + 1 );

        if( stc )
        {
            stc->EndUndoAction();

            // Programmatic insertion emits modification events but no CHARADDED.  Preserve
            // KiCad's text-variable/SPICE completion at the caret inside the new pair.
            wxStyledTextEvent added( wxEVT_STC_CHARADDED, stc->GetId() );
            added.SetEventObject( stc );
            added.SetKey( key );
            added.SetPosition( stc->GetCurrentPos() );
            stc->GetEventHandler()->ProcessEvent( added );
        }
    }

    std::unordered_set<wxWindow*> m_windows;

    wxDECLARE_DYNAMIC_CLASS( TEXT_ENTRY_AUTOPAIR );
};

wxIMPLEMENT_DYNAMIC_CLASS( TEXT_ENTRY_AUTOPAIR, wxModule );
