/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2014-2019 CERN
 * @author Maciej Suminski <maciej.suminski@cern.ch>
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


#include <sch_draw_panel.h>

#include <wx/debug.h>
#include <wx/event.h>
#include <wx/gdicmn.h>
#include <wx/window.h>
#include <wx/windowid.h>
#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>

#include <confirm.h>
#include <eda_draw_frame.h>
#include <gal/definitions.h>
#include <gal/graphics_abstraction_layer.h>
#include <layer_ids.h>
#include <math/vector2d.h>
#include <pgm_base.h>
#include <settings/settings_manager.h>
#include <view/view.h>
#include <view/view_controls.h>
#include <view/wx_view_controls.h>

#include <sch_base_frame.h>
#include <sch_painter.h>
#include <sch_scope.h>
#include <sch_screen.h>
#include <sch_symbol.h>
#include <zoom_defines.h>


SCH_DRAW_PANEL::SCH_DRAW_PANEL( wxWindow* aParentWindow, wxWindowID aWindowId,
                                const wxPoint& aPosition, const wxSize& aSize,
                                KIGFX::GAL_DISPLAY_OPTIONS& aOptions, GAL_TYPE aGalType )
        : EDA_DRAW_PANEL_GAL( aParentWindow, aWindowId, aPosition, aSize, aOptions, aGalType )
{
    m_view = new KIGFX::SCH_VIEW( dynamic_cast<SCH_BASE_FRAME*>( GetParentEDAFrame() ) );
    m_view->SetGAL( m_gal );

    m_gal->SetWorldUnitLength( SCH_WORLD_UNIT );

    m_painter.reset( new KIGFX::SCH_PAINTER( m_gal ) );

    COLOR_SETTINGS* cs = ::GetColorSettings( DEFAULT_THEME );

    if( SCH_BASE_FRAME* frame = dynamic_cast<SCH_BASE_FRAME*>( GetParentEDAFrame() ) )
        cs = frame->GetColorSettings();

    wxASSERT( cs );
    m_painter->GetSettings()->LoadColors( cs );

    m_view->SetPainter( m_painter.get() );

    // This fixes the zoom in and zoom out limits:
    m_view->SetScaleLimits( ZOOM_MAX_LIMIT_EESCHEMA, ZOOM_MIN_LIMIT_EESCHEMA );
    m_view->SetMirror( false, false );

    // Early initialization of the canvas background color,
    // before any OnPaint event is fired for the canvas using a wrong bg color
    auto settings = m_painter->GetSettings();
    m_gal->SetClearColor( settings->GetBackgroundColor() );

    setDefaultLayerOrder();
    setDefaultLayerDeps();

    GetView()->UpdateAllLayersOrder();

    // View controls is the first in the event handler chain, so the Tool Framework operates
    // on updated viewport data.
    m_viewControls = new KIGFX::WX_VIEW_CONTROLS( m_view, this );

    Bind( wxEVT_MOUSEWHEEL, &SCH_DRAW_PANEL::onScopeMouseWheel, this );
    Bind( wxEVT_LEFT_DOWN, &SCH_DRAW_PANEL::onScopeLeftDown, this );
    Bind( wxEVT_LEFT_UP, &SCH_DRAW_PANEL::onScopeLeftUp, this );
    Bind( wxEVT_MIDDLE_DOWN, &SCH_DRAW_PANEL::onScopeMiddleDown, this );
    Bind( wxEVT_MIDDLE_UP, &SCH_DRAW_PANEL::onScopeMiddleUp, this );
    Bind( wxEVT_MOTION, &SCH_DRAW_PANEL::onScopeMotion, this );
    Bind( wxEVT_MOUSE_CAPTURE_LOST, &SCH_DRAW_PANEL::onScopeCaptureLost, this );

    SetEvtHandlerEnabled( true );
    SetFocus();
    Show( true );
    Raise();
    StartDrawing();
}


SCH_DRAW_PANEL::~SCH_DRAW_PANEL()
{
}


SCH_SYMBOL* SCH_DRAW_PANEL::scopeAt( const wxPoint& aPosition ) const
{
    SCH_BASE_FRAME* frame = dynamic_cast<SCH_BASE_FRAME*>( GetParentEDAFrame() );
    SCH_SCREEN*     screen = frame ? frame->GetScreen() : nullptr;

    if( !screen )
        return nullptr;

    const VECTOR2I worldPosition = KiROUND( GetView()->ToWorld(
            VECTOR2D( aPosition.x, aPosition.y ) ) );

    for( SCH_ITEM* item : screen->Items().OfType( SCH_SYMBOL_T ) )
    {
        SCH_SYMBOL* symbol = static_cast<SCH_SYMBOL*>( item );
        BOX2I       bbox = symbol->GetBodyBoundingBox();
        bbox.Normalize();

        if( SCH_SCOPE::IsScopeSymbol( symbol ) && bbox.Contains( worldPosition ) )
            return symbol;
    }

    return nullptr;
}


void SCH_DRAW_PANEL::refreshScope( SCH_SYMBOL* aScope )
{
    if( !aScope )
        return;

    GetView()->Update( aScope, KIGFX::REPAINT );
    Refresh();
}


void SCH_DRAW_PANEL::onScopeMouseWheel( wxMouseEvent& aEvent )
{
    SCH_SYMBOL* scope = scopeAt( aEvent.GetPosition() );

    if( !scope || aEvent.AltDown()
        || ( aEvent.ControlDown() && aEvent.ShiftDown() ) )
    {
        aEvent.Skip();
        return;
    }

    const double steps = static_cast<double>( aEvent.GetWheelRotation() )
                         / std::max( 1, aEvent.GetWheelDelta() );
    bool changed = false;

    if( aEvent.GetWheelAxis() == wxMOUSE_WHEEL_HORIZONTAL || aEvent.ControlDown() )
    {
        changed = SCH_SCOPE::PanViewport( scope, VECTOR2D( steps * 0.12, 0.0 ) );
    }
    else if( aEvent.ShiftDown() )
    {
        changed = SCH_SCOPE::PanViewport( scope, VECTOR2D( 0.0, steps * 0.12 ) );
    }
    else
    {
        BOX2I plotBox = SCH_SCOPE::GetLayout( scope ).plotBox;

        if( plotBox.GetWidth() > 0 && plotBox.GetHeight() > 0 )
        {
            const VECTOR2D world = GetView()->ToWorld(
                    VECTOR2D( aEvent.GetX(), aEvent.GetY() ) );
            const VECTOR2D anchor( ( world.x - plotBox.GetX() ) / plotBox.GetWidth(),
                                   1.0 - ( world.y - plotBox.GetY() ) / plotBox.GetHeight() );
            changed = SCH_SCOPE::ZoomViewport( scope, anchor, std::pow( 1.25, steps ) );
        }
    }

    if( changed )
        refreshScope( scope );
}


void SCH_DRAW_PANEL::onScopeLeftDown( wxMouseEvent& aEvent )
{
    SCH_SYMBOL* scope = scopeAt( aEvent.GetPosition() );

    if( !scope || aEvent.ControlDown() || aEvent.ShiftDown() || aEvent.AltDown() )
    {
        aEvent.Skip();
        return;
    }

    const BOX2I plotBox = SCH_SCOPE::GetLayout( scope ).plotBox;
    const VECTOR2D world = GetView()->ToWorld(
            VECTOR2D( aEvent.GetX(), aEvent.GetY() ) );

    if( plotBox.GetWidth() <= 0 || plotBox.GetHeight() <= 0
        || !plotBox.Contains( KiROUND( world ) ) )
    {
        aEvent.Skip();
        return;
    }

    m_scopeZoomTarget = scope;
    SCH_SCOPE::BeginZoomSelection(
            scope, VECTOR2D( ( world.x - plotBox.GetX() ) / plotBox.GetWidth(),
                             1.0 - ( world.y - plotBox.GetY() ) / plotBox.GetHeight() ) );

    if( !HasCapture() )
        CaptureMouse();

    refreshScope( scope );
}


void SCH_DRAW_PANEL::onScopeLeftUp( wxMouseEvent& aEvent )
{
    if( !m_scopeZoomTarget )
    {
        aEvent.Skip();
        return;
    }

    SCH_SYMBOL* scope = m_scopeZoomTarget;
    const BOX2I plotBox = SCH_SCOPE::GetLayout( scope ).plotBox;
    const VECTOR2D world = GetView()->ToWorld(
            VECTOR2D( aEvent.GetX(), aEvent.GetY() ) );
    SCH_SCOPE::UpdateZoomSelection(
            scope, VECTOR2D( ( world.x - plotBox.GetX() ) / plotBox.GetWidth(),
                             1.0 - ( world.y - plotBox.GetY() ) / plotBox.GetHeight() ) );
    SCH_SCOPE::FinishZoomSelection( scope );
    m_scopeZoomTarget = nullptr;

    if( HasCapture() )
        ReleaseMouse();

    refreshScope( scope );
}


void SCH_DRAW_PANEL::onScopeMiddleDown( wxMouseEvent& aEvent )
{
    m_scopePanTarget = scopeAt( aEvent.GetPosition() );

    if( !m_scopePanTarget )
    {
        aEvent.Skip();
        return;
    }

    m_scopePanLast = aEvent.GetPosition();

    if( !HasCapture() )
        CaptureMouse();
}


void SCH_DRAW_PANEL::onScopeMiddleUp( wxMouseEvent& aEvent )
{
    if( !m_scopePanTarget )
    {
        aEvent.Skip();
        return;
    }

    m_scopePanTarget = nullptr;

    if( HasCapture() )
        ReleaseMouse();
}


void SCH_DRAW_PANEL::onScopeMotion( wxMouseEvent& aEvent )
{
    if( m_scopeZoomTarget )
    {
        if( !aEvent.LeftIsDown() )
        {
            SCH_SCOPE::CancelZoomSelection( m_scopeZoomTarget );
            m_scopeZoomTarget = nullptr;

            if( HasCapture() )
                ReleaseMouse();

            return;
        }

        const BOX2I plotBox = SCH_SCOPE::GetLayout( m_scopeZoomTarget ).plotBox;
        const VECTOR2D world = GetView()->ToWorld(
                VECTOR2D( aEvent.GetX(), aEvent.GetY() ) );

        if( SCH_SCOPE::UpdateZoomSelection(
                    m_scopeZoomTarget,
                    VECTOR2D( ( world.x - plotBox.GetX() ) / plotBox.GetWidth(),
                              1.0 - ( world.y - plotBox.GetY() ) / plotBox.GetHeight() ) ) )
        {
            refreshScope( m_scopeZoomTarget );
        }

        return;
    }

    if( !m_scopePanTarget )
    {
        aEvent.Skip();
        return;
    }

    if( !aEvent.MiddleIsDown() )
    {
        m_scopePanTarget = nullptr;

        if( HasCapture() )
            ReleaseMouse();

        return;
    }

    const wxPoint current = aEvent.GetPosition();
    const wxPoint delta = current - m_scopePanLast;
    BOX2I         plotBox = SCH_SCOPE::GetLayout( m_scopePanTarget ).plotBox;
    const double pixelWidth = std::max( 1.0, plotBox.GetWidth() * m_gal->GetWorldScale() );
    const double pixelHeight = std::max( 1.0, plotBox.GetHeight() * m_gal->GetWorldScale() );

    m_scopePanLast = current;

    if( SCH_SCOPE::PanViewport( m_scopePanTarget,
                                VECTOR2D( -delta.x / pixelWidth, delta.y / pixelHeight ) ) )
    {
        refreshScope( m_scopePanTarget );
    }
}


void SCH_DRAW_PANEL::onScopeCaptureLost( wxMouseCaptureLostEvent& aEvent )
{
    if( !m_scopePanTarget && !m_scopeZoomTarget )
    {
        aEvent.Skip();
        return;
    }

    if( m_scopeZoomTarget )
        SCH_SCOPE::CancelZoomSelection( m_scopeZoomTarget );

    m_scopePanTarget = nullptr;
    m_scopeZoomTarget = nullptr;
}


void SCH_DRAW_PANEL::DisplaySymbol( LIB_SYMBOL* aSymbol )
{
    GetView()->DisplaySymbol( aSymbol );
}


void SCH_DRAW_PANEL::DisplaySheet( SCH_SCREEN *aScreen )
{
    GetView()->Clear();

    if( aScreen )
        GetView()->DisplaySheet( aScreen );
    else
        GetView()->Cleanup();
}


void SCH_DRAW_PANEL::setDefaultLayerOrder()
{
    for( int i = 0; (unsigned) i < sizeof( SCH_LAYER_ORDER ) / sizeof( int ); ++i )
    {
        int layer = SCH_LAYER_ORDER[i];
        wxASSERT( layer < KIGFX::VIEW::VIEW_MAX_LAYERS );

        m_view->SetLayerOrder( layer, i );
    }
}


bool SCH_DRAW_PANEL::SwitchBackend( GAL_TYPE aGalType )
{
    bool rv = EDA_DRAW_PANEL_GAL::SwitchBackend( aGalType );
    setDefaultLayerDeps();
    m_gal->SetWorldUnitLength( SCH_WORLD_UNIT );

    Refresh();

    return rv;
}


void SCH_DRAW_PANEL::setDefaultLayerDeps()
{
    // caching makes no sense for Cairo and other software renderers
    auto target = m_backend == GAL_TYPE_OPENGL ? KIGFX::TARGET_CACHED : KIGFX::TARGET_NONCACHED;

    for( int i = 0; i < KIGFX::VIEW::VIEW_MAX_LAYERS; i++ )
        m_view->SetLayerTarget( i, target );

    m_view->SetLayerTarget( LAYER_SCHEMATIC_ANCHOR, KIGFX::TARGET_NONCACHED );
    m_view->SetLayerDisplayOnly( LAYER_SCHEMATIC_ANCHOR );

    // Bitmaps are draw on a non cached GAL layer:
    m_view->SetLayerTarget( LAYER_DRAW_BITMAPS, KIGFX::TARGET_NONCACHED );

    // Some draw layers need specific settings
    m_view->SetLayerTarget( LAYER_GP_OVERLAY, KIGFX::TARGET_OVERLAY );
    m_view->SetLayerDisplayOnly( LAYER_GP_OVERLAY ) ;

    m_view->SetLayerTarget( LAYER_SELECT_OVERLAY, KIGFX::TARGET_OVERLAY );
    m_view->SetLayerDisplayOnly( LAYER_SELECT_OVERLAY ) ;

    m_view->SetLayerTarget( LAYER_DRAWINGSHEET, KIGFX::TARGET_NONCACHED );
    m_view->SetLayerDisplayOnly( LAYER_DRAWINGSHEET ) ;

    m_view->SetLayerTarget( LAYER_OP_VOLTAGES, KIGFX::TARGET_OVERLAY );
    m_view->SetLayerDisplayOnly( LAYER_OP_VOLTAGES );
    m_view->SetLayerTarget( LAYER_OP_CURRENTS, KIGFX::TARGET_OVERLAY );
    m_view->SetLayerDisplayOnly( LAYER_OP_CURRENTS );

    m_view->SetLayerTarget( LAYER_SELECTION_SHADOWS, KIGFX::TARGET_OVERLAY );
    m_view->SetLayerDisplayOnly( LAYER_SELECTION_SHADOWS ) ;

    m_view->SetLayerDisplayOnly( LAYER_NET_COLOR_HIGHLIGHT );
    m_view->SetLayerDisplayOnly( LAYER_DANGLING );
}


KIGFX::SCH_VIEW* SCH_DRAW_PANEL::GetView() const
{
    return static_cast<KIGFX::SCH_VIEW*>( m_view );
}


void SCH_DRAW_PANEL::OnShow()
{
    SCH_BASE_FRAME* frame = dynamic_cast<SCH_BASE_FRAME*>( GetParentEDAFrame() );

    try
    {
        // Check if the current rendering backend can be properly initialized
        m_view->UpdateItems();
    }
    catch( const std::runtime_error& e )
    {
        DisplayInfoMessage( frame, e.what() );

        // Use fallback if one is available
        if( GAL_FALLBACK != m_backend )
        {
            SwitchBackend( GAL_FALLBACK );

            if( frame )
                frame->ActivateGalCanvas();
        }
    }
}


void SCH_DRAW_PANEL::onPaint( wxPaintEvent& aEvent )
{
    // The first wxPaintEvent can be fired at startup before the GAL engine is fully initialized
    // (depending on platforms). Do nothing in this case
    if( !m_gal->IsInitialized() || !m_gal->IsVisible() )
        return;

    EDA_DRAW_PANEL_GAL::onPaint( aEvent );
}
