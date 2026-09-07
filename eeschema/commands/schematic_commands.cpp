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

#include "schematic_commands.h"

#include <base_units.h>
#include <command/command.h>
#include <sch_edit_frame.h>
#include <sch_screen.h>
#include <sch_symbol.h>
#include <schematic.h>
#include <tool/tool_manager.h>
#include <tools/sch_point_editor.h>
#include <tools/sch_selection_tool.h>

namespace KICAD_COMMAND
{
namespace
{
using JSON = nlohmann::json;

class SCHEMATIC_CONTEXT : public CONTEXT
{
public:
    explicit SCHEMATIC_CONTEXT( SCH_EDIT_FRAME& aFrame ) : m_frame( aFrame ) {}

    EDITOR Editor() const override { return EDITOR::SCHEMATIC; }

    JSON Describe() const override
    {
        JSON result = { { "editor", "schematic" }, { "document", nullptr },
                        { "selection", JSON::array() }, { "units", "mm" } };

        if( !m_frame.GetToolManager() || !m_frame.Schematic().HasHierarchy() )
            return result;

        const SCHEMATIC&      schematic = m_frame.Schematic();
        const SCH_SHEET_PATH& sheet = schematic.CurrentSheet();

        result["document"] = schematic.GetFileName().ToStdString( wxConvUTF8 );
        result["modified"] = m_frame.IsContentModified();
        result["sheet_path"] = sheet.PathAsString().ToStdString( wxConvUTF8 );
        result["sheet_name"] = sheet.PathHumanReadable().ToStdString( wxConvUTF8 );
        result["variant"] = schematic.GetCurrentVariant().ToStdString( wxConvUTF8 );
        result["busy"] = !CheckReady( EFFECT::QUERY ).IsOk();

        for( EDA_ITEM* item : m_frame.GetCurrentSelection() )
            result["selection"].push_back( item->m_Uuid.AsString().ToStdString( wxConvUTF8 ) );

        return result;
    }

    RESULT CheckReady( EFFECT ) const override
    {
        TOOL_MANAGER* manager = m_frame.GetToolManager();

        if( !manager || !m_frame.Schematic().HasHierarchy()
            || !m_frame.Schematic().GetCurrentScreen() )
        {
            return RESULT::Error( STATUS::NO_DOCUMENT, _( "No schematic is open." ) );
        }

        TOOL_BASE*        currentTool = manager->GetCurrentTool();
        SCH_POINT_EDITOR* pointEditor = manager->GetTool<SCH_POINT_EDITOR>();
        bool passivePointEditor = pointEditor && currentTool == pointEditor
                                  && !pointEditor->IsDragging();

        // Schematic API readiness currently only checks whether the frame is enabled.
        // Passive point editing is safe, but queries must not expose an unfinished edit.
        if( !m_frame.CanAcceptApiCommands() || !m_frame.ToolStackIsEmpty()
            || ( currentTool != manager->GetTool<SCH_SELECTION_TOOL>() && !passivePointEditor ) )
        {
            return RESULT::Error( STATUS::BUSY,
                                  _( "Finish the active editor operation before running commands." ) );
        }

        return RESULT::Ok( wxEmptyString );
    }

    SCH_EDIT_FRAME& Frame() const { return m_frame; }

private:
    SCH_EDIT_FRAME& m_frame;
};


JSON describeSymbol( const SCH_SYMBOL& aSymbol, const SCH_SHEET_PATH& aSheet,
                     const wxString& aVariant )
{
    const VECTOR2I position = aSymbol.GetPosition();

    return { { "uuid", aSymbol.m_Uuid.AsString().ToStdString( wxConvUTF8 ) },
             { "reference", aSymbol.GetRef( &aSheet ).ToStdString( wxConvUTF8 ) },
             { "reference_with_unit", aSymbol.GetRef( &aSheet, true ).ToStdString( wxConvUTF8 ) },
             { "unit", aSymbol.GetUnitSelection( &aSheet ) },
             { "value", aSymbol.GetValue( false, &aSheet, false, aVariant ).ToStdString( wxConvUTF8 ) },
             { "library_id", aSymbol.GetSymbolIDAsString().ToStdString( wxConvUTF8 ) },
             { "footprint", aSymbol.GetFootprintFieldText( false, &aSheet, false, aVariant )
                                    .ToStdString( wxConvUTF8 ) },
             { "sheet_path", aSheet.PathAsString().ToStdString( wxConvUTF8 ) },
             { "sheet_name", aSheet.PathHumanReadable().ToStdString( wxConvUTF8 ) },
             { "position", { { "x", schIUScale.IUTomm( position.x ) },
                               { "y", schIUScale.IUTomm( position.y ) } } } };
}


RESULT querySymbols( CONTEXT& aContext, const std::vector<wxString>& aArgs )
{
    const SCHEMATIC& schematic = static_cast<SCHEMATIC_CONTEXT&>( aContext ).Frame().Schematic();
    const wxString  variant = schematic.GetCurrentVariant();
    const bool      filtered = !aArgs.empty();
    JSON            symbols = JSON::array();

    if( filtered && aArgs.front().IsEmpty() )
        return RESULT::Error( STATUS::INVALID_ARGUMENT, _( "A symbol reference is required." ) );

    // Traverse instances, not unique screens: a reused sheet may have different references
    // and units. SCH_REFERENCE_LIST is deliberately avoided because constructing annotation
    // references can repair missing instance data and thus mutate an otherwise read-only query.
    for( const SCH_SHEET_PATH& sheet : schematic.Hierarchy() )
    {
        SCH_SCREEN* screen = sheet.LastScreen();

        if( !screen )
            continue;

        for( SCH_ITEM* item : screen->Items().OfType( SCH_SYMBOL_T ) )
        {
            const SCH_SYMBOL& symbol = *static_cast<SCH_SYMBOL*>( item );

            if( filtered && !symbol.GetRef( &sheet ).IsSameAs( aArgs.front(), false )
                && !symbol.GetRef( &sheet, true ).IsSameAs( aArgs.front(), false ) )
            {
                continue;
            }

            symbols.push_back( describeSymbol( symbol, sheet, variant ) );
        }
    }

    if( filtered && symbols.empty() )
    {
        return RESULT::Error( STATUS::NOT_FOUND,
                              wxString::Format( _( "No symbol matches '%s'." ), aArgs.front() ) );
    }

    size_t count = symbols.size();

    return RESULT::Ok( wxString::Format( _( "%zu symbol instance(s)." ), count ),
                       { { "symbols", std::move( symbols ) }, { "count", count },
                         { "units", "mm" }, { "variant", variant.ToStdString( wxConvUTF8 ) } } );
}
} // namespace


std::unique_ptr<EXECUTOR> CreateSchematicCommandExecutor( SCH_EDIT_FRAME& aFrame )
{
    auto executor = std::make_unique<EXECUTOR>( std::make_unique<SCHEMATIC_CONTEXT>( aFrame ) );

    executor->Registry().Register(
            { "schematic.list_symbols", "schematic.list_symbols",
              _( "List live symbol instances across all sheets, including power symbols." ),
              EDITOR::SCHEMATIC, EFFECT::QUERY, 0, 0, querySymbols } );
    executor->Registry().Register(
            { "schematic.get", "schematic.get <reference>",
              _( "Get every matching symbol instance; a unit suffix such as U1A narrows the result." ),
              EDITOR::SCHEMATIC, EFFECT::QUERY, 1, 1, querySymbols } );

    return executor;
}
} // namespace KICAD_COMMAND
