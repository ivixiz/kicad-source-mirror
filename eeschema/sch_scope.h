/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
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
 * along with this program; if not, you may find one here:
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 * or you may search the http://www.gnu.org website for the version 2 license,
 * or you may write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#ifndef SCH_SCOPE_H
#define SCH_SCOPE_H

#include <lib_id.h>
#include <sch_shape.h>
#include <stroke_params.h>

#include <utility>
#include <vector>


class LIB_SYMBOL;
class PLOTTER;
class SCHEMATIC;
class SCH_SHEET_PATH;
class SCH_SYMBOL;


/**
 * Schematic waveform canvas.
 *
 * A placed scope is a SCH_SYMBOL with LibId() and a single SCH_SCOPE rectangle in its local
 * library symbol.  This class deliberately owns the scope-specific data model, serialization,
 * geometry calculations, waveform decimation and PDF plotting.  Editor tools own wx event
 * handling, SCH_PAINTER owns GAL calls, and SIMULATOR_FRAME owns ngspice data acquisition.
 *
 * SETTINGS is persisted in a hidden field on the placed symbol.  Samples, viewport and cursor
 * state are runtime-only and are keyed by the symbol UUID; they must never be written into the
 * schematic file.  This keeps scope configuration portable while avoiding large schematic files
 * and stale simulation data after a reload.
 */
class SCH_SCOPE : public SCH_SHAPE
{
public:
#if 0
    // Deprecated: the first scope prototype used differential channel pins and local V/I/P,
    // add, and remove controls.  The implementation is kept in sch_scope.cpp as a reference,
    // but scopes are now pinless waveform canvases configured through their properties dialog.
    enum class CONTROL
    {
        NONE,
        CHANNEL_MODE,
        ADD_CHANNEL,
        REMOVE_CHANNEL
    };

    struct CONTROL_HIT
    {
        CONTROL control = CONTROL::NONE;
        int     channel = 0;
    };
#endif

    /** Runtime samples supplied by SIMULATOR_FRAME after a completed simulation. */
    struct WAVEFORM
    {
        wxString           name;
        std::vector<double> x;
        std::vector<double> y;
        double             minX = 0.0;
        double             maxX = 1.0;
        double             minY = 0.0;
        double             maxY = 1.0;
        bool               monotonicX = false;
    };

    /** Persisted display settings for one simulator signal.  Source order is z-order. */
    struct WAVEFORM_SOURCE
    {
        wxString       name;
        KIGFX::COLOR4D color;
        int            lineWidth = 0;

        bool operator==( const WAVEFORM_SOURCE& aOther ) const;
    };

    /**
     * Persisted scope appearance and selected signals.
     *
     * The defaults are intentionally independent of the editor color theme: a scope is a
     * document object and therefore preserves its explicitly selected appearance.
     */
    struct SETTINGS
    {
        std::vector<WAVEFORM_SOURCE> sources;
        KIGFX::COLOR4D               backgroundColor;
        KIGFX::COLOR4D               borderColor;
        int                          borderWidth = 0;
        bool                         gridVisible = true;
        LINE_STYLE                   gridStyle = LINE_STYLE::SOLID;
        KIGFX::COLOR4D               gridColor;
        int                          gridWidth = 0;
        bool                         minorGridVisible = true;
        LINE_STYLE                   minorGridStyle = LINE_STYLE::SOLID;
        KIGFX::COLOR4D               minorGridColor;
        int                          minorGridWidth = 0;
        wxString                     axisFontName;
        int                          axisTextSize = 0;

        bool operator==( const SETTINGS& aOther ) const;
        bool operator!=( const SETTINGS& aOther ) const { return !( *this == aOther ); }
    };

    /** Normalized visible data interval.  [0, 1] displays the complete simulation result. */
    struct VIEWPORT
    {
        double xMin = 0.0;
        double xMax = 1.0;
        double yMin = 0.0;
        double yMax = 1.0;
    };

    /** Cached bounds of the current runtime samples, including the small vertical headroom. */
    struct DATA_BOUNDS
    {
        double minX = 0.0;
        double maxX = 1.0;
        double minY = 0.0;
        double maxY = 1.0;
    };

    /** Names derived from the currently selected simulator analysis. */
    struct AXIS_INFO
    {
        wxString xName = wxS( "X" );
        wxString yName = wxS( "Amplitude" );
    };

    /**
     * World-coordinate layout shared by screen hit testing, GAL painting and PDF plotting.
     *
     * Keeping this calculation in the model prevents renderer-specific margins and legend hit
     * boxes from drifting apart when the scope is resized.
     */
    struct LAYOUT
    {
        BOX2I bodyBox;
        BOX2I plotBox;
        int   margin = 0;
        int   textSize = 0;
        int   lineHeight = 0;
        int   legendTop = 0;
        int   legendRows = 0;
        int   legendColumns = 1;
        int   xDivisions = 5;
        int   yDivisions = 5;
    };

    /** Transient normalized rectangle used for the LTspice-style zoom selection gesture. */
    struct ZOOM_SELECTION
    {
        bool     active = false;
        VECTOR2D start;
        VECTOR2D end;
    };

    /**
     * A crosshair cursor in normalized viewport coordinates.  Its y value follows the topmost
     * visible waveform so a cursor remains useful while the view is panned or zoomed.
     */
    struct CURSOR
    {
        double x = 0.0;
        double y = 0.0;
        bool   valid = false;
    };

    /** Precomputed labels and placements for cursor readouts and delta arrows. */
    struct CURSOR_MEASUREMENT
    {
        bool     valid = false;
        bool     arrowVisible = false;
        double   arrowY = 0.0;
        bool     yDeltaVisible = false;
        double   yDeltaX = 0.0;
        wxString frequencyLabel;
        wxString periodLabel;
        wxString yDeltaLabel;
        bool     singleCursorValid = false;
        wxString cursorXLabel;
        wxString cursorYLabel;
    };

    SCH_SCOPE( const VECTOR2I& aPosition = VECTOR2I( 0, 0 ), SCH_LAYER_ID aLayer = LAYER_DEVICE,
               int aLineWidth = 0, FILL_T aFillType = FILL_T::FILLED_WITH_COLOR );

    /** Default and smallest permitted size for a newly placed scope canvas. */
    static VECTOR2I DefaultSize();
    static VECTOR2I MinimumSize();

    /** Library identity used to distinguish canvas symbols from ordinary schematic symbols. */
    static LIB_ID LibId();
    static bool IsScopeSymbol( const SCH_SYMBOL* aSymbol );

    /**
     * Create the pinless local symbol used by the Place Scope tool.
     *
     * The caller owns the returned symbol and is responsible for adding it to a screen and an
     * undo commit.  Keeping construction here makes the representation invariant independent of
     * the placement workflow.
     */
    static SCH_SYMBOL* CreateSymbol( SCHEMATIC* aSchematic, const SCH_SHEET_PATH& aSheetPath,
                                     const VECTOR2I& aPosition );

    /**
     * Migrate a placed scope to the pinless canvas representation and synchronize its body
     * shape with SETTINGS.  This is intentionally idempotent and may be called on load, edit or
     * before simulation refresh.
     */
    static bool NormalizeCanvasSymbol( SCH_SYMBOL* aSymbol );

    /** Persisted configuration access.  SetSettings preserves runtime samples and viewport. */
    static SETTINGS DefaultSettings();
    static KIGFX::COLOR4D DefaultWaveformColor( size_t aIndex );
    static SETTINGS GetSettings( const SCH_SYMBOL* aSymbol );
    static void SetSettings( SCH_SYMBOL* aSymbol, const SETTINGS& aSettings );

    /** Compatibility helpers for callers that only need the selected signal names. */
    static std::vector<wxString> GetWaveformSources( const SCH_SYMBOL* aSymbol );
    static void SetWaveformSources( SCH_SYMBOL* aSymbol,
                                    const std::vector<wxString>& aSources );

    /** Runtime waveform access.  SetWaveforms sanitizes samples and recomputes data bounds. */
    static const std::vector<WAVEFORM>* GetWaveforms( const SCH_SYMBOL* aSymbol );
    static const WAVEFORM* GetWaveform( const SCH_SYMBOL* aSymbol, const wxString& aName );
    static void SetWaveforms( const SCH_SYMBOL* aSymbol, std::vector<WAVEFORM> aWaveforms );
    static void ClearWaveforms( const SCH_SYMBOL* aSymbol );
    static DATA_BOUNDS GetDataBounds( const SCH_SYMBOL* aSymbol );
    static AXIS_INFO GetAxisInfo( const SCH_SYMBOL* aSymbol );
    static void SetAxisInfo( const SCH_SYMBOL* aSymbol, const AXIS_INFO& aInfo );

    /** Crosshair lifetime and measurements.  At most two cursors are retained per scope. */
    static std::vector<CURSOR> GetCursors( const SCH_SYMBOL* aSymbol );
    static int AddCursor( const SCH_SYMBOL* aSymbol, double aNormalizedX );
    static bool MoveCursor( const SCH_SYMBOL* aSymbol, int aIndex, double aNormalizedX );
    static bool RemoveCursor( const SCH_SYMBOL* aSymbol, int aIndex );
    static void BeginCursorMove( const SCH_SYMBOL* aSymbol );
    static void FinishCursorMove( const SCH_SYMBOL* aSymbol );
    static CURSOR_MEASUREMENT GetCursorMeasurement( const SCH_SYMBOL* aSymbol );
    static bool CursorXToPlot( const CURSOR& aCursor, const VIEWPORT& aViewport,
                               const BOX2I& aPlotBox, int& aPosition );
    static bool CursorToPlot( const CURSOR& aCursor, const VIEWPORT& aViewport,
                              const BOX2I& aPlotBox, VECTOR2I& aPosition );
    /** Formatting helpers shared by the screen renderer and vector PDF renderer. */
    static wxString FormatEngineeringValue( double aValue );
    static wxString FormatWaveformLabel( const wxString& aSource );
    static wxString FitLegendLabel( const wxString& aLabel, int aAvailableWidth,
                                    int aTextSize );
    static std::vector<wxString> FormatEngineeringTicks( double aMin, double aMax,
                                                         int aDivisions );

    /** Shared geometry, legend hit testing and bounded waveform decimation. */
    static LAYOUT GetLayout( const SCH_SYMBOL* aSymbol );
    static int HitTestLegend( const SCH_SYMBOL* aSymbol, const VECTOR2I& aPosition );
    static std::vector<std::pair<VECTOR2I, VECTOR2I>> BuildWaveformSegments(
            const WAVEFORM& aWaveform, const DATA_BOUNDS& aBounds, const VIEWPORT& aViewport,
            const BOX2I& aPlotBox, size_t aBucketCount );
    /**
     * Emit the complete scope canvas to a PLOTTER for schematic export.
     *
     * SCH_PAINTER has a matching GAL-specific draw path.  The two backends intentionally remain
     * separate because GAL and PLOTTER have different clipping, font and depth APIs; all data
     * calculations they share are exposed above instead of being duplicated in the renderers.
     */
    static void PlotWaveforms( PLOTTER* aPlotter, const SCH_SYMBOL* aSymbol );

    /** Runtime viewport and box-zoom operations.  They never modify the schematic. */
    static VIEWPORT GetViewport( const SCH_SYMBOL* aSymbol );
    static bool ZoomViewport( const SCH_SYMBOL* aSymbol, const VECTOR2D& aAnchor,
                              double aFactor );
    static bool PanViewport( const SCH_SYMBOL* aSymbol, const VECTOR2D& aDelta );
    static void ResetViewport( const SCH_SYMBOL* aSymbol );
    static ZOOM_SELECTION GetZoomSelection( const SCH_SYMBOL* aSymbol );
    static void BeginZoomSelection( const SCH_SYMBOL* aSymbol, const VECTOR2D& aPosition );
    static bool UpdateZoomSelection( const SCH_SYMBOL* aSymbol, const VECTOR2D& aPosition );
    static bool FinishZoomSelection( const SCH_SYMBOL* aSymbol );
    static void CancelZoomSelection( const SCH_SYMBOL* aSymbol );

    /** Scope placement/resize grid; retained separately from the canvas display grid. */
    static int GridSize();

#if 0
    // Deprecated channel-based scope API.  See the disabled implementation in sch_scope.cpp.
    static VECTOR2I MinimumSize( int aChannelCount );
    static int ChannelCount( const LIB_SYMBOL* aSymbol );
    static void UpdateChannelModeControl( LIB_SYMBOL* aSymbol, const VECTOR2I& aBodySize );
    static void UpdateChannelGeometry( LIB_SYMBOL* aSymbol, const VECTOR2I& aBodySize );
    static CONTROL_HIT HitTestControl( const SCH_SYMBOL* aSymbol, const VECTOR2I& aPosition );
    static bool HitTestChannelModeControl( const SCH_SYMBOL* aSymbol, const VECTOR2I& aPosition );
    static bool CycleChannelMode( SCH_SYMBOL* aSymbol );
    static bool CycleChannelMode( SCH_SYMBOL* aSymbol, int aChannel );
    static bool AddChannel( SCH_SYMBOL* aSymbol );
    static bool RemoveChannel( SCH_SYMBOL* aSymbol );
    static int MaxChannelCount();
    static VECTOR2I FirstPinOffset();
    static int PinLength();
    static int PinPitch();
    static int PinTextSize();
    static int PinNameOffset();
    static int ChannelModeTextSize();
    static int ChannelModeButtonSize();
#endif

    wxString GetClass() const override;

    wxString GetFriendlyName() const override;

    wxString GetItemDescription( UNITS_PROVIDER* aUnitsProvider, bool aFull ) const override;

    BITMAPS GetMenuImage() const override;

    EDA_ITEM* Clone() const override;
};


#endif /* SCH_SCOPE_H */
