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

#ifndef KICAD_COMMAND_WINDOW_H
#define KICAD_COMMAND_WINDOW_H

#include <command/command.h>
#include <wx/panel.h>

class EDA_BASE_FRAME;
class EDA_DRAW_FRAME;
class wxTextCtrl;
class wxStaticText;

/**
 * Docked textual frontend for an editor's semantic command executor.
 *
 * The panel owns its executor and session history. It knows nothing about board or schematic
 * objects; editor adapters resolve the current document every time a command is submitted.
 */
class COMMAND_WINDOW : public wxPanel
{
public:
    static void Install( EDA_DRAW_FRAME* aFrame,
                         std::unique_ptr<KICAD_COMMAND::EXECUTOR> aExecutor );
    static void RestoreSettings( EDA_BASE_FRAME* aFrame );
    static void SaveSettings( EDA_BASE_FRAME* aFrame );
    static void Toggle( EDA_BASE_FRAME* aFrame );
    static bool IsShown( EDA_BASE_FRAME* aFrame );

    ~COMMAND_WINDOW() override;

private:
    COMMAND_WINDOW( EDA_DRAW_FRAME* aFrame,
                    std::unique_ptr<KICAD_COMMAND::EXECUTOR> aExecutor );

    void onSubmit( wxCommandEvent& aEvent );
    void onCharHook( wxKeyEvent& aEvent );
    void onLanguageChange( wxCommandEvent& aEvent );
    void appendOutput( const wxString& aText );

    EDA_DRAW_FRAME*                           m_frame;
    std::unique_ptr<KICAD_COMMAND::EXECUTOR>   m_executor;
    KICAD_COMMAND::HISTORY                    m_history;
    wxTextCtrl*                              m_output;
    wxTextCtrl*                              m_input;
    wxStaticText*                            m_hint;
    bool                                     m_executing = false;
};

#endif // KICAD_COMMAND_WINDOW_H
