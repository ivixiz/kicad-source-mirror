/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "pcb_commands.h"

#include <base_units.h>
#include <board.h>
#include <board_commit.h>
#include <command/command.h>
#include <footprint.h>
#include <netinfo.h>
#include <pcb_edit_frame.h>
#include <pcb_marker.h>
#include <rc_item.h>
#include <tool/tool_manager.h>
#include <tools/drc_tool.h>
#include <trigo.h>

#include <array>
#include <cmath>
#include <limits>
#include <locale>
#include <sstream>

namespace KICAD_COMMAND
{
namespace
{
using JSON = nlohmann::json;
using ARGS = std::vector<wxString>;


class PCB_CONTEXT : public CONTEXT
{
public:
    explicit PCB_CONTEXT( PCB_EDIT_FRAME& aFrame ) : m_frame( aFrame ) {}

    EDITOR Editor() const override { return EDITOR::PCB; }
    PCB_EDIT_FRAME& Frame() const { return m_frame; }
    BOARD& Board() const { return *m_frame.GetBoard(); }

    JSON Describe() const override
    {
        JSON data = { { "editor", "pcb" }, { "document", nullptr }, { "units", "mm" },
                      { "coordinate_origin", "internal" }, { "positive_y", "down" },
                      { "selection", JSON::array() } };

        if( BOARD* board = m_frame.GetBoard() )
        {
            data["document"] = board->GetFileName().ToStdString( wxConvUTF8 );
            data["modified"] = m_frame.IsContentModified();

            if( m_frame.GetToolManager() )
            {
                for( EDA_ITEM* item : m_frame.GetCurrentSelection() )
                    data["selection"].push_back( item->m_Uuid.AsString().ToStdString() );
            }
        }

        return data;
    }

    RESULT CheckReady( EFFECT aEffect ) const override
    {
        if( !m_frame.GetBoard() || !m_frame.GetToolManager() )
            return RESULT::Error( STATUS::NO_DOCUMENT, _( "No PCB document is open." ) );

        DRC_TOOL* drc = m_frame.GetToolManager()->GetTool<DRC_TOOL>();

        if( !m_frame.CanAcceptApiCommands() || ( drc && drc->IsDRCRunning() ) )
            return RESULT::Error( STATUS::BUSY, _( "Finish the current PCB operation first." ) );

        return RESULT::Ok( wxEmptyString );
    }

private:
    PCB_EDIT_FRAME& m_frame;
};


PCB_CONTEXT& pcbContext( CONTEXT& aContext )
{
    return static_cast<PCB_CONTEXT&>( aContext );
}


JSON footprintData( const FOOTPRINT& aFootprint )
{
    const VECTOR2I pos = aFootprint.GetPosition();

    return { { "id", aFootprint.m_Uuid.AsString().ToStdString() },
             { "reference", aFootprint.GetReference().ToStdString( wxConvUTF8 ) },
             { "value", aFootprint.GetValue().ToStdString( wxConvUTF8 ) },
             { "library_id", aFootprint.GetFPIDAsString().ToStdString( wxConvUTF8 ) },
             { "x_mm", pcbIUScale.IUTomm( pos.x ) },
             { "y_mm", pcbIUScale.IUTomm( pos.y ) },
             { "rotation_degrees", aFootprint.GetOrientationDegrees() },
             { "layer", aFootprint.GetBoard()->GetLayerName( aFootprint.GetLayer() )
                                .ToStdString( wxConvUTF8 ) },
             { "locked", aFootprint.IsLocked() } };
}


RESULT findFootprint( BOARD& aBoard, const wxString& aReference, FOOTPRINT*& aFootprint )
{
    aFootprint = nullptr;

    // References are user data and need not be unique; never silently edit the first match.
    for( FOOTPRINT* footprint : aBoard.Footprints() )
    {
        if( footprint->GetReference() == aReference )
        {
            if( aFootprint )
            {
                return RESULT::Error( STATUS::AMBIGUOUS,
                                      _( "More than one footprint has this reference." ) );
            }

            aFootprint = footprint;
        }
    }

    if( !aFootprint )
        return RESULT::Error( STATUS::NOT_FOUND, _( "Footprint reference was not found." ) );

    return RESULT::Ok( wxEmptyString );
}


bool parseNumber( const wxString& aText, double& aValue )
{
    // A classic stream accepts an optional leading '+' and never changes the process locale.
    std::istringstream stream( aText.ToStdString( wxConvUTF8 ) );
    stream.imbue( std::locale::classic() );
    stream >> std::noskipws >> aValue;
    return !stream.fail() && stream.eof() && std::isfinite( aValue );
}


bool validCoordinate( double aValue )
{
    // Match the interactive edit tool's safety margin for geometry near integer limits.
    constexpr double limit = std::numeric_limits<int>::max() - pcbIUScale.mmToIU( 20 );
    return std::isfinite( aValue ) && aValue >= -limit && aValue <= limit;
}


std::array<VECTOR2D, 4> corners( const FOOTPRINT& aFootprint )
{
    const BOX2I box = aFootprint.GetBoundingBox();
    const VECTOR2D pos( box.GetPosition() );
    const VECTOR2D end = pos + VECTOR2D( box.GetSize() );
    return { pos, VECTOR2D( end.x, pos.y ), end, VECTOR2D( pos.x, end.y ) };
}


RESULT moveFootprint( CONTEXT& aContext, const ARGS& aArgs )
{
    double x, y;

    if( !parseNumber( aArgs[1], x ) || !parseNumber( aArgs[2], y ) )
        return RESULT::Error( STATUS::INVALID_ARGUMENT, _( "Coordinates must be finite numbers." ) );

    double scale = pcbIUScale.IU_PER_MM;

    if( aArgs.size() == 4 )
    {
        if( aArgs[3] == wxS( "mil" ) )
            scale = pcbIUScale.IU_PER_MILS;
        else if( aArgs[3] == wxS( "in" ) )
            scale = pcbIUScale.IU_PER_MILS * 1000;
        else if( aArgs[3] != wxS( "mm" ) )
            return RESULT::Error( STATUS::INVALID_ARGUMENT, _( "Units must be mm, mil or in." ) );
    }

    x = std::round( x * scale );
    y = std::round( y * scale );

    if( !validCoordinate( x ) || !validCoordinate( y ) )
        return RESULT::Error( STATUS::INVALID_ARGUMENT, _( "Position exceeds PCB coordinate limits." ) );

    PCB_CONTEXT& context = pcbContext( aContext );
    FOOTPRINT* footprint;
    RESULT found = findFootprint( context.Board(), aArgs[0], footprint );

    if( !found.IsOk() )
        return found;

    if( footprint->IsLocked() )
        return RESULT::Error( STATUS::LOCKED, _( "The footprint is locked." ) );

    const VECTOR2I position( static_cast<int>( x ), static_cast<int>( y ) );

    if( position == footprint->GetPosition() )
        return RESULT::Ok( _( "Footprint is already at this position." ), footprintData( *footprint ) );

    const VECTOR2D delta = VECTOR2D( position ) - VECTOR2D( footprint->GetPosition() );

    if( !validCoordinate( delta.x ) || !validCoordinate( delta.y ) )
        return RESULT::Error( STATUS::INVALID_ARGUMENT, _( "Move exceeds PCB coordinate limits." ) );

    for( const VECTOR2D& corner : corners( *footprint ) )
    {
        if( !validCoordinate( corner.x + delta.x ) || !validCoordinate( corner.y + delta.y ) )
            return RESULT::Error( STATUS::INVALID_ARGUMENT, _( "Footprint exceeds PCB coordinate limits." ) );
    }

    BOARD_COMMIT commit( &context.Frame() );
    commit.Modify( footprint );
    RESULT result;

    try
    {
        footprint->SetPosition( position );
        footprint->InvalidateComponentClassCache();
        result = RESULT::Ok( _( "Footprint moved." ), footprintData( *footprint ), true );
    }
    catch( ... )
    {
        commit.Revert();
        throw;
    }

    // Push transfers the saved copies to undo; they must not be reverted after that handoff.
    commit.Push( _( "Move Footprint via Command Window" ) );
    return result;
}


RESULT rotateFootprint( CONTEXT& aContext, const ARGS& aArgs )
{
    double degrees;

    if( !parseNumber( aArgs[1], degrees ) )
        return RESULT::Error( STATUS::INVALID_ARGUMENT, _( "Angle must be a finite number of degrees." ) );

    PCB_CONTEXT& context = pcbContext( aContext );
    FOOTPRINT* footprint;
    RESULT found = findFootprint( context.Board(), aArgs[0], footprint );

    if( !found.IsOk() )
        return found;

    if( footprint->IsLocked() )
        return RESULT::Error( STATUS::LOCKED, _( "The footprint is locked." ) );

    degrees = std::fmod( degrees, 360.0 );

    if( degrees == 0.0 )
        return RESULT::Ok( _( "Footprint orientation is unchanged." ), footprintData( *footprint ) );

    const EDA_ANGLE angle( degrees, DEGREES_T );
    const VECTOR2I center = footprint->GetPosition();

    for( VECTOR2D corner : corners( *footprint ) )
    {
        RotatePoint( &corner.x, &corner.y, center.x, center.y, angle );

        if( !validCoordinate( corner.x ) || !validCoordinate( corner.y ) )
            return RESULT::Error( STATUS::INVALID_ARGUMENT, _( "Rotated footprint exceeds PCB coordinate limits." ) );
    }

    BOARD_COMMIT commit( &context.Frame() );
    commit.Modify( footprint );
    RESULT result;

    try
    {
        footprint->Rotate( center, angle );
        footprint->InvalidateComponentClassCache();
        result = RESULT::Ok( _( "Footprint rotated." ), footprintData( *footprint ), true );
    }
    catch( ... )
    {
        commit.Revert();
        throw;
    }

    // Push transfers the saved copies to undo; they must not be reverted after that handoff.
    commit.Push( _( "Rotate Footprint via Command Window" ) );
    return result;
}


RESULT runDrc( CONTEXT& aContext, const ARGS& )
{
    PCB_CONTEXT& context = pcbContext( aContext );
    DRC_TOOL* tool = context.Frame().GetToolManager()->GetTool<DRC_TOOL>();

    if( !tool )
        return RESULT::Error( STATUS::FAILED, _( "The design rule checker is unavailable." ) );

    const DRC_RUN_RESULT outcome = tool->RunTestsFromCommand();

    switch( outcome )
    {
    case DRC_RUN_RESULT::BUSY:
        return RESULT::Error( STATUS::BUSY, _( "The design rule checker is busy." ) );
    case DRC_RUN_RESULT::INVALID_RULES:
        return RESULT::Error( STATUS::FAILED, _( "DRC could not compile the custom design rules." ) );
    case DRC_RUN_RESULT::FAILED:
        return RESULT::Error( STATUS::FAILED, _( "DRC failed before completing the checks." ) );
    default:
        break;
    }

    const bool completed = outcome == DRC_RUN_RESULT::COMPLETED;
    JSON violations = JSON::array();
    size_t errors = 0;
    size_t warnings = 0;
    size_t exclusions = 0;

    for( const PCB_MARKER* marker : context.Board().Markers() )
    {
        const std::shared_ptr<RC_ITEM> item = marker->GetRCItem();
        const SEVERITY severity = marker->GetSeverity();
        const char* severityName = "info";

        if( marker->IsExcluded() || severity == RPT_SEVERITY_EXCLUSION )
        {
            ++exclusions;
            severityName = "excluded";
        }
        else if( severity == RPT_SEVERITY_ERROR )
        {
            ++errors;
            severityName = "error";
        }
        else if( severity == RPT_SEVERITY_WARNING )
        {
            ++warnings;
            severityName = "warning";
        }

        JSON ids = JSON::array();

        for( const KIID& id : item->GetIDs() )
        {
            if( id != niluuid )
                ids.push_back( id.AsString().ToStdString() );
        }

        violations.push_back( { { "id", marker->m_Uuid.AsString().ToStdString() },
                                { "code", item->GetErrorCode() }, { "severity", severityName },
                                { "message", item->GetErrorMessage( false ).ToStdString( wxConvUTF8 ) },
                                { "x_mm", pcbIUScale.IUTomm( marker->GetPos().x ) },
                                { "y_mm", pcbIUScale.IUTomm( marker->GetPos().y ) },
                                { "items", std::move( ids ) } } );
    }

    RESULT result = completed
                            ? RESULT::Ok( _( "DRC completed. Results use the current DRC dialog options." ) )
                            : RESULT::Error( STATUS::CANCELLED, _( "DRC cancelled; results are incomplete." ) );
    result.data = { { "completed", completed }, { "errors", errors }, { "warnings", warnings },
                    { "excluded", exclusions }, { "violations", std::move( violations ) } };
    result.changed = true; // The live check markers were replaced; document content is not saved.
    return result;
}
} // namespace


std::unique_ptr<EXECUTOR> CreatePcbCommandExecutor( PCB_EDIT_FRAME& aFrame )
{
    auto executor = std::make_unique<EXECUTOR>( std::make_unique<PCB_CONTEXT>( aFrame ) );
    REGISTRY& registry = executor->Registry();

    registry.Register( { wxS( "pcb.list_footprints" ), wxS( "pcb.list_footprints" ),
                         _( "List footprints in the live PCB document." ), EDITOR::PCB,
                         EFFECT::QUERY, 0, 0,
                         []( CONTEXT& context, const ARGS& )
                         {
                             JSON footprints = JSON::array();

                             for( FOOTPRINT* fp : pcbContext( context ).Board().Footprints() )
                                 footprints.push_back( footprintData( *fp ) );

                             return RESULT::Ok( _( "Footprints (positions in mm)." ),
                                                { { "footprints", std::move( footprints ) } } );
                         } } );

    registry.Register( { wxS( "pcb.get" ), wxS( "pcb.get <reference>" ),
                         _( "Get one footprint by its exact, unique reference." ), EDITOR::PCB,
                         EFFECT::QUERY, 1, 1,
                         []( CONTEXT& context, const ARGS& args )
                         {
                             FOOTPRINT* fp;
                             RESULT found = findFootprint( pcbContext( context ).Board(), args[0], fp );
                             return found.IsOk() ? RESULT::Ok( _( "Footprint." ), footprintData( *fp ) )
                                                 : found;
                         } } );

    registry.Register( { wxS( "pcb.move" ), wxS( "pcb.move <reference> <x> <y> [mm|mil|in]" ),
                         _( "Move to absolute internal coordinates; default mm, positive Y down." ),
                         EDITOR::PCB, EFFECT::EDIT, 3, 4, moveFootprint } );

    registry.Register( { wxS( "pcb.rotate" ), wxS( "pcb.rotate <reference> <degrees>" ),
                         _( "Rotate counterclockwise about the footprint anchor by the given angle." ),
                         EDITOR::PCB, EFFECT::EDIT, 2, 2, rotateFootprint } );

    registry.Register( { wxS( "pcb.list_nets" ), wxS( "pcb.list_nets" ),
                         _( "List named PCB nets (net zero is unconnected and omitted)." ),
                         EDITOR::PCB, EFFECT::QUERY, 0, 0,
                         []( CONTEXT& context, const ARGS& )
                         {
                             JSON nets = JSON::array();

                             for( const auto& [code, net] :
                                  pcbContext( context ).Board().GetNetInfo().NetsByNetcode() )
                             {
                                 if( code > 0 )
                                     nets.push_back( { { "code", code },
                                                       { "name", net->GetNetname().ToStdString( wxConvUTF8 ) } } );
                             }

                             return RESULT::Ok( _( "PCB nets." ), { { "nets", std::move( nets ) } } );
                         } } );

    registry.Register( { wxS( "drc.run" ), wxS( "drc.run" ),
                         _( "Run DRC on the live board with the current dialog options and cancellation UI." ),
                         EDITOR::PCB, EFFECT::CHECK, 0, 0, runDrc } );

    return executor;
}
} // namespace KICAD_COMMAND
