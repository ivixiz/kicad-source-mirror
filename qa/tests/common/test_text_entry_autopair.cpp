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

#define BOOST_TEST_NO_MAIN
#include <boost/test/unit_test.hpp>

#include <wx/combobox.h>
#include <wx/display.h>
#include <wx/frame.h>
#include <wx/grid.h>
#include <wx/srchctrl.h>
#include <wx/stc/stc.h>
#include <wx/textctrl.h>
#include <wx/valtext.h>


namespace
{
bool typeChar( wxWindow* aWindow, int aChar, int aModifiers = wxMOD_NONE )
{
    wxKeyEvent event( wxEVT_CHAR );
    event.SetEventObject( aWindow );
    event.m_keyCode = aChar;
    event.m_uniChar = aChar;
    event.SetControlDown( aModifiers & wxMOD_CONTROL );
    event.SetAltDown( aModifiers & wxMOD_ALT );
    event.SetShiftDown( aModifiers & wxMOD_SHIFT );
    return aWindow->ProcessWindowEvent( event );
}


struct TEXT_ENTRY_FIXTURE
{
    TEXT_ENTRY_FIXTURE()
    {
        if( wxDisplay::GetCount() > 0 )
            frame = new wxFrame( nullptr, wxID_ANY, "Text entry pairing test" );
    }

    ~TEXT_ENTRY_FIXTURE()
    {
        if( frame )
            frame->Destroy();
    }

    wxFrame* frame = nullptr;
};
} // namespace


BOOST_FIXTURE_TEST_SUITE( TextEntryAutoPair, TEXT_ENTRY_FIXTURE )

BOOST_AUTO_TEST_CASE( NativeTextEntries )
{
    if( !frame )
        return;

    wxWindow* controls[] = {
        new wxTextCtrl( frame, wxID_ANY ),
        new wxTextCtrl( frame, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE ),
        new wxComboBox( frame, wxID_ANY ),
        new wxSearchCtrl( frame, wxID_ANY )
    };

    for( wxWindow* control : controls )
    {
        auto* entry = dynamic_cast<wxTextEntryBase*>( control );
        BOOST_REQUIRE( entry );

        for( const wxString& pair : { "()", "[]", "{}", "''", "\"\"", "``" } )
        {
            entry->ChangeValue( "" );
            BOOST_CHECK( typeChar( control, pair[0] ) );
            BOOST_CHECK_EQUAL( entry->GetValue(), pair );
            BOOST_CHECK_EQUAL( entry->GetInsertionPoint(), 1 );

            BOOST_CHECK( typeChar( control, pair[1] ) );
            BOOST_CHECK_EQUAL( entry->GetValue(), pair );
            BOOST_CHECK_EQUAL( entry->GetInsertionPoint(), 2 );
        }
    }
}


BOOST_AUTO_TEST_CASE( NestedPairsSelectionAndEscapes )
{
    if( !frame )
        return;

    auto* entry = new wxTextCtrl( frame, wxID_ANY );
    typeChar( entry, '(' );
    typeChar( entry, '[' );
    BOOST_CHECK_EQUAL( entry->GetValue(), "([])" );
    BOOST_CHECK_EQUAL( entry->GetInsertionPoint(), 2 );

    wxString text = wxString::FromUTF8( "\xD0\xA2\xD0\xBE\xD0\xBA" );
    entry->ChangeValue( text );
    entry->SelectAll();
    typeChar( entry, '"' );
    BOOST_CHECK_EQUAL( entry->GetValue(), "\"" + text + "\"" );
    BOOST_CHECK_EQUAL( entry->GetStringSelection(), text );

    entry->ChangeValue( "\\" );
    entry->SetInsertionPointEnd();
    BOOST_CHECK( !typeChar( entry, '"' ) );
    BOOST_CHECK_EQUAL( entry->GetValue(), "\\" );

    entry->ChangeValue( "\\\\" );
    entry->SetInsertionPointEnd();
    typeChar( entry, '"' );
    BOOST_CHECK_EQUAL( entry->GetValue(), "\\\\\"\"" );
}


BOOST_AUTO_TEST_CASE( ValidatorsAndNonEditableControls )
{
    if( !frame )
        return;

    wxTextValidator digits( wxFILTER_DIGITS );
    auto* numeric = new wxTextCtrl( frame, wxID_ANY, "", wxDefaultPosition, wxDefaultSize,
                                    0, digits );
    typeChar( numeric, '(' );
    BOOST_CHECK( numeric->IsEmpty() );

    wxTextValidator noClosing( wxFILTER_EXCLUDE_CHAR_LIST );
    noClosing.SetCharExcludes( ")" );
    numeric->SetValidator( noClosing );
    BOOST_CHECK( !typeChar( numeric, '(' ) );
    BOOST_CHECK( numeric->IsEmpty() );

    auto* entry = new wxTextCtrl( frame, wxID_ANY );
    entry->SetEditable( false );
    typeChar( entry, '(' );
    BOOST_CHECK( entry->IsEmpty() );
    entry->SetEditable( true );
    entry->Disable();
    typeChar( entry, '(' );
    BOOST_CHECK( entry->IsEmpty() );

    auto* password = new wxTextCtrl( frame, wxID_ANY, "", wxDefaultPosition, wxDefaultSize,
                                     wxTE_PASSWORD );
    BOOST_CHECK( !typeChar( password, '(' ) );
    BOOST_CHECK( password->IsEmpty() );
}


BOOST_AUTO_TEST_CASE( ShortcutsAltGrAndProgrammaticText )
{
    if( !frame )
        return;

    auto* entry = new wxTextCtrl( frame, wxID_ANY );
    BOOST_CHECK( !typeChar( entry, '[', wxMOD_CONTROL ) );
    BOOST_CHECK( entry->IsEmpty() );
    typeChar( entry, '[', wxMOD_CONTROL | wxMOD_ALT );
    BOOST_CHECK_EQUAL( entry->GetValue(), "[]" );

    entry->ChangeValue( "(" );
    BOOST_CHECK_EQUAL( entry->GetValue(), "(" );
    entry->SetInsertionPointEnd();
    entry->WriteText( "{" );
    BOOST_CHECK_EQUAL( entry->GetValue(), "({" );
}


BOOST_AUTO_TEST_CASE( ScintillaUnicodeUndoAndCompletion )
{
    if( !frame )
        return;

    auto* editor = new wxStyledTextCtrl( frame, wxID_ANY );
    wxString text = wxString::FromUTF8( "\xD0\xA2\xD0\xBE\xD0\xBA" );
    editor->SetText( text );
    editor->SetSelection( 0, editor->GetTextLength() );
    editor->EmptyUndoBuffer();
    typeChar( editor, '(' );
    BOOST_CHECK_EQUAL( editor->GetText(), "(" + text + ")" );
    BOOST_CHECK_EQUAL( editor->GetSelectedText(), text );
    editor->Undo();
    BOOST_CHECK_EQUAL( editor->GetText(), text );
    BOOST_CHECK( !editor->CanUndo() );
    editor->Redo();
    BOOST_CHECK_EQUAL( editor->GetText(), "(" + text + ")" );

    editor->SetText( text );
    editor->GotoPos( editor->GetTextLength() );
    typeChar( editor, '[' );
    BOOST_CHECK_EQUAL( editor->GetText(), text + "[]" );
    BOOST_CHECK_EQUAL( editor->GetCurrentPos(), editor->GetTextLength() - 1 );

    editor->SetText( "$" );
    editor->GotoPos( 1 );
    int notifications = 0;
    editor->Bind( wxEVT_STC_CHARADDED,
                  [&]( wxStyledTextEvent& event )
                  {
                      BOOST_CHECK_EQUAL( event.GetKey(), '{' );
                      BOOST_CHECK_EQUAL( editor->GetText(), "${}" );
                      BOOST_CHECK_EQUAL( editor->GetCurrentPos(), 2 );
                      ++notifications;
                  } );
    typeChar( editor, '{' );
    BOOST_CHECK_EQUAL( notifications, 1 );
    editor->AutoCompShow( 0, "VALUE" );
    typeChar( editor, '}' );
    BOOST_CHECK_EQUAL( editor->GetText(), "${VALUE}" );
    BOOST_CHECK_EQUAL( editor->GetCurrentPos(), editor->GetTextLength() );
}


BOOST_AUTO_TEST_CASE( GridEditorFirstCharacter )
{
    if( !frame )
        return;

    auto* grid = new wxGrid( frame, wxID_ANY );
    grid->CreateGrid( 1, 2 );
    grid->SetGridCursor( 0, 0 );
    BOOST_CHECK( typeChar( grid->GetGridWindow(), '(' ) );
    BOOST_REQUIRE( grid->IsCellEditControlEnabled() );

    wxGridCellEditorPtr editor( grid->GetCellEditor( 0, 0 ) );
    auto* entry = dynamic_cast<wxTextEntryBase*>( editor->GetControl() );
    BOOST_REQUIRE( entry );
    BOOST_CHECK_EQUAL( entry->GetValue(), "()" );
    BOOST_CHECK_EQUAL( entry->GetInsertionPoint(), 1 );

    grid->SaveEditControlValue();
    grid->DisableCellEditControl();
    BOOST_CHECK_EQUAL( grid->GetCellValue( 0, 0 ), "()" );

    grid->SetReadOnly( 0, 1 );
    grid->SetGridCursor( 0, 1 );
    typeChar( grid->GetGridWindow(), '(' );
    BOOST_CHECK( !grid->IsCellEditControlEnabled() );
    BOOST_CHECK( grid->GetCellValue( 0, 1 ).empty() );
}

BOOST_AUTO_TEST_SUITE_END()
