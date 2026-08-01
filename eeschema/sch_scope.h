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
class SCH_SYMBOL;


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

    struct WAVEFORM_SOURCE
    {
        wxString       name;
        KIGFX::COLOR4D color;
        int            lineWidth = 0;

        bool operator==( const WAVEFORM_SOURCE& aOther ) const;
    };

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

    struct VIEWPORT
    {
        double xMin = 0.0;
        double xMax = 1.0;
        double yMin = 0.0;
        double yMax = 1.0;
    };

    struct DATA_BOUNDS
    {
        double minX = 0.0;
        double maxX = 1.0;
        double minY = 0.0;
        double maxY = 1.0;
    };

    struct AXIS_INFO
    {
        wxString xName = wxS( "X" );
        wxString yName = wxS( "Amplitude" );
    };

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

    struct ZOOM_SELECTION
    {
        bool     active = false;
        VECTOR2D start;
        VECTOR2D end;
    };

    struct CURSOR
    {
        double x = 0.0;
        double y = 0.0;
        bool   valid = false;
    };

    struct CURSOR_MEASUREMENT
    {
        bool     valid = false;
        bool     arrowVisible = false;
        double   arrowY = 0.0;
        wxString frequencyLabel;
        wxString periodLabel;
    };

    SCH_SCOPE( const VECTOR2I& aPosition = VECTOR2I( 0, 0 ), SCH_LAYER_ID aLayer = LAYER_DEVICE,
               int aLineWidth = 0, FILL_T aFillType = FILL_T::FILLED_WITH_COLOR );

    static VECTOR2I DefaultSize();
    static VECTOR2I MinimumSize();
    static LIB_ID LibId();
    static bool IsScopeSymbol( const SCH_SYMBOL* aSymbol );
    static bool NormalizeCanvasSymbol( SCH_SYMBOL* aSymbol );

    static SETTINGS DefaultSettings();
    static KIGFX::COLOR4D DefaultWaveformColor( size_t aIndex );
    static SETTINGS GetSettings( const SCH_SYMBOL* aSymbol );
    static void SetSettings( SCH_SYMBOL* aSymbol, const SETTINGS& aSettings );

    static std::vector<wxString> GetWaveformSources( const SCH_SYMBOL* aSymbol );
    static void SetWaveformSources( SCH_SYMBOL* aSymbol,
                                    const std::vector<wxString>& aSources );
    static const std::vector<WAVEFORM>* GetWaveforms( const SCH_SYMBOL* aSymbol );
    static void SetWaveforms( const SCH_SYMBOL* aSymbol, std::vector<WAVEFORM> aWaveforms );
    static void ClearWaveforms( const SCH_SYMBOL* aSymbol );
    static DATA_BOUNDS GetDataBounds( const SCH_SYMBOL* aSymbol );
    static AXIS_INFO GetAxisInfo( const SCH_SYMBOL* aSymbol );
    static void SetAxisInfo( const SCH_SYMBOL* aSymbol, const AXIS_INFO& aInfo );
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
    static wxString FormatEngineeringValue( double aValue );
    static wxString FormatWaveformLabel( const wxString& aSource );
    static wxString FitLegendLabel( const wxString& aLabel, int aAvailableWidth,
                                    int aTextSize );
    static std::vector<wxString> FormatEngineeringTicks( double aMin, double aMax,
                                                         int aDivisions );

    static LAYOUT GetLayout( const SCH_SYMBOL* aSymbol );
    static int HitTestLegend( const SCH_SYMBOL* aSymbol, const VECTOR2I& aPosition );
    static std::vector<std::pair<VECTOR2I, VECTOR2I>> BuildWaveformSegments(
            const WAVEFORM& aWaveform, const DATA_BOUNDS& aBounds, const VIEWPORT& aViewport,
            const BOX2I& aPlotBox, size_t aBucketCount );
    static void PlotWaveforms( PLOTTER* aPlotter, const SCH_SYMBOL* aSymbol );

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
