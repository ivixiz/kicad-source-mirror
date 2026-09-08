/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include <widgets/command_window.h>

#include <algorithm>
#include <class_draw_panel_gal.h>
#include <eda_draw_frame.h>
#include <scoped_set_reset.h>
#include <settings/app_settings.h>
#include <widgets/ui_common.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/wupdlock.h>

namespace
{
const wxString PANE_NAME = wxS( "CommandWindow" );
constexpr long MAX_OUTPUT_LENGTH = 200000;
}


COMMAND_WINDOW::COMMAND_WINDOW( EDA_DRAW_FRAME* aFrame,
                                std::unique_ptr<KICAD_COMMAND::EXECUTOR> aExecutor ) :
        wxPanel( aFrame ),
        m_frame( aFrame ),
        m_executor( std::move( aExecutor ) )
{
    wxBoxSizer* sizer = new wxBoxSizer( wxVERTICAL );

    m_output = new wxTextCtrl( this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                              wxTE_MULTILINE | wxTE_READONLY | wxTE_RICH2 | wxTE_DONTWRAP );
    m_output->SetName( _( "Command output" ) );
    m_output->SetFont( KIUI::GetMonospacedUIFont() );
    sizer->Add( m_output, 1, wxEXPAND | wxALL, FromDIP( 4 ) );

    wxBoxSizer* inputSizer = new wxBoxSizer( wxHORIZONTAL );
    inputSizer->Add( new wxStaticText( this, wxID_ANY, wxS( ">" ) ),
                     0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP( 4 ) );

    m_input = new wxTextCtrl( this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                             wxTE_PROCESS_ENTER );
    m_input->SetName( _( "Command input" ) );
    m_input->SetFont( KIUI::GetMonospacedUIFont() );
    inputSizer->Add( m_input, 1, wxEXPAND );
    sizer->Add( inputSizer, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP( 4 ) );

    m_hint = new wxStaticText( this, wxID_ANY,
                              _( "Enter a command. Type help for commands; Up/Down for history." ) );
    sizer->Add( m_hint, 0, wxALL, FromDIP( 4 ) );
    SetSizer( sizer );

    m_input->Bind( wxEVT_TEXT_ENTER, &COMMAND_WINDOW::onSubmit, this );
    Bind( wxEVT_CHAR_HOOK, &COMMAND_WINDOW::onCharHook, this );
    m_frame->Bind( EDA_LANG_CHANGED, &COMMAND_WINDOW::onLanguageChange, this );
}


COMMAND_WINDOW::~COMMAND_WINDOW()
{
    m_frame->Unbind( EDA_LANG_CHANGED, &COMMAND_WINDOW::onLanguageChange, this );
}


void COMMAND_WINDOW::Install( EDA_DRAW_FRAME* aFrame,
                              std::unique_ptr<KICAD_COMMAND::EXECUTOR> aExecutor )
{
    wxAuiManager* manager = wxAuiManager::GetManager( aFrame );
    wxCHECK_RET( manager && aExecutor, wxS( "Command Window requires an editor and executor" ) );

    if( manager->GetPane( PANE_NAME ).IsOk() )
        return;

    const auto& cfg = aFrame->config()->m_CommandWindow;
    COMMAND_WINDOW* panel = new COMMAND_WINDOW( aFrame, std::move( aExecutor ) );

    manager->AddPane( panel, EDA_PANE().Name( PANE_NAME )
                      .Caption( _( "Command Window" ) )
                      .Bottom().Layer( 2 )
                      .MinSize( aFrame->FromDIP( wxSize( 300, 120 ) ) )
                      .BestSize( aFrame->FromDIP( wxSize( 600, cfg.height ) ) )
                      .FloatingSize( aFrame->FromDIP( wxSize( 700, cfg.height ) ) )
                      .CloseButton( true ).DestroyOnClose( false )
                      .Show( cfg.show ) );
}


void COMMAND_WINDOW::RestoreSettings( EDA_BASE_FRAME* aFrame )
{
    if( wxAuiManager* manager = wxAuiManager::GetManager( aFrame ) )
    {
        wxAuiPaneInfo& pane = manager->GetPane( PANE_NAME );

        if( pane.IsOk() )
            pane.Show( aFrame->config()->m_CommandWindow.show );
    }
}


void COMMAND_WINDOW::SaveSettings( EDA_BASE_FRAME* aFrame )
{
    if( wxAuiManager* manager = wxAuiManager::GetManager( aFrame ) )
    {
        wxAuiPaneInfo& pane = manager->GetPane( PANE_NAME );

        if( pane.IsOk() )
        {
            auto& cfg = aFrame->config()->m_CommandWindow;
            cfg.show = pane.IsShown();

            if( pane.IsShown() && pane.window )
                cfg.height = std::clamp( aFrame->ToDIP( pane.window->GetSize().y ), 120, 1200 );
        }
    }
}


bool COMMAND_WINDOW::IsShown( EDA_BASE_FRAME* aFrame )
{
    if( wxAuiManager* manager = wxAuiManager::GetManager( aFrame ) )
    {
        const wxAuiPaneInfo& pane = manager->GetPane( PANE_NAME );
        return pane.IsOk() && pane.IsShown();
    }

    return false;
}


void COMMAND_WINDOW::Toggle( EDA_BASE_FRAME* aFrame )
{
    wxAuiManager* manager = wxAuiManager::GetManager( aFrame );

    if( !manager )
        return;

    wxAuiPaneInfo& pane = manager->GetPane( PANE_NAME );

    if( !pane.IsOk() )
        return;

    COMMAND_WINDOW* panel = static_cast<COMMAND_WINDOW*>( pane.window );
    SaveSettings( aFrame );
    pane.Show( !pane.IsShown() );
    manager->Update();

    if( pane.IsShown() )
        panel->m_input->SetFocus();
    else
        panel->m_frame->GetCanvas()->SetFocus();
}


void COMMAND_WINDOW::appendOutput( const wxString& aText )
{
    wxWindowUpdateLocker freeze( m_output );
    wxString text = aText;

    if( text.length() > static_cast<size_t>( MAX_OUTPUT_LENGTH ) )
    {
        const wxString notice = _( "\n[Output truncated]\n" );
        text = text.Left( MAX_OUTPUT_LENGTH - notice.length() ) + notice;
    }

    const long excess = m_output->GetLastPosition() + static_cast<long>( text.length() )
                        - MAX_OUTPUT_LENGTH;

    if( excess > 0 )
        m_output->Remove( 0, excess );

    m_output->AppendText( text );
    m_output->ShowPosition( m_output->GetLastPosition() );
}


void COMMAND_WINDOW::onSubmit( wxCommandEvent& aEvent )
{
    if( m_executing )
        return;

    const wxString command = m_input->GetValue();
    wxString trimmed = command;

    if( trimmed.Trim( true ).Trim( false ).IsEmpty() )
        return;

    m_history.Add( command );
    m_input->ChangeValue( wxEmptyString );
    appendOutput( wxS( "> " ) + command + wxS( "\n" ) );

    SCOPED_SET_RESET executing( m_executing, true );
    SCOPED_EXECUTION<std::function<void()>> inputGuard(
            [this] { m_input->Disable(); },
            [this]
            {
                m_input->Enable();

                if( IsShownOnScreen() )
                    m_input->SetFocus();
            } );

    const KICAD_COMMAND::RESULT result = m_executor->Execute( command );

    wxString output;

    if( !result.IsOk() )
        output << wxS( "[" ) << KICAD_COMMAND::StatusName( result.status ) << wxS( "] " );

    output << result.message << wxS( "\n" );

    if( !result.data.empty() )
    {
        output << wxString::FromUTF8( result.data.dump(
                          2, ' ', false, nlohmann::json::error_handler_t::replace ) ) << wxS( "\n" );
    }

    appendOutput( output );
}


void COMMAND_WINDOW::onCharHook( wxKeyEvent& aEvent )
{
    if( wxWindow::FindFocus() == m_input && !aEvent.HasAnyModifiers() )
    {
        if( aEvent.GetKeyCode() == WXK_UP )
        {
            m_input->ChangeValue( m_history.Previous( m_input->GetValue() ) );
            m_input->SetInsertionPointEnd();
            return;
        }

        if( aEvent.GetKeyCode() == WXK_DOWN && m_history.IsBrowsing() )
        {
            m_input->ChangeValue( m_history.Next() );
            m_input->SetInsertionPointEnd();
            return;
        }

        if( aEvent.GetKeyCode() == WXK_ESCAPE )
        {
            m_frame->GetCanvas()->SetFocus();
            return;
        }
    }

    // Text navigation and clipboard shortcuts belong to the native text controls.
    aEvent.Skip();
}


void COMMAND_WINDOW::onLanguageChange( wxCommandEvent& aEvent )
{
    m_input->SetName( _( "Command input" ) );
    m_output->SetName( _( "Command output" ) );
    m_hint->SetLabel( _( "Enter a command. Type help for commands; Up/Down for history." ) );

    if( wxAuiManager* manager = wxAuiManager::GetManager( m_frame ) )
    {
        manager->GetPane( PANE_NAME ).Caption( _( "Command Window" ) );
        manager->Update();
    }

    Layout();
    aEvent.Skip();
}
