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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <dialogs/dialog_color_picker.h>

#include <wx/button.h>
#include <wx/dcbuffer.h>
#include <wx/dcscreen.h>
#include <wx/display.h>
#include <wx/image.h>
#include <wx/panel.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/timer.h>
#include <wx/utils.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <utility>


using KIGFX::COLOR4D;


namespace
{

constexpr size_t MAX_RECENT_COLORS = 20;


int toByte( double aValue )
{
    return std::lround( std::clamp( aValue, 0.0, 1.0 ) * 255.0 );
}


COLOR4D fromHSV( double aHue, double aSaturation, double aValue, double aAlpha = 1.0 )
{
    COLOR4D color;
    color.FromHSV( aHue, aSaturation, aValue );
    color.a = std::clamp( aAlpha, 0.0, 1.0 );
    return color;
}


bool isConcreteColor( const COLOR4D& aColor )
{
    return !aColor.m_text && aColor != COLOR4D::UNSPECIFIED;
}


bool sameConcreteColor( const COLOR4D& aLeft, const COLOR4D& aRight )
{
    return isConcreteColor( aLeft ) && isConcreteColor( aRight )
           && toByte( aLeft.r ) == toByte( aRight.r )
           && toByte( aLeft.g ) == toByte( aRight.g )
           && toByte( aLeft.b ) == toByte( aRight.b )
           && toByte( aLeft.a ) == toByte( aRight.a );
}


COLOR4D editableColor( const COLOR4D& aColor, const COLOR4D& aDefault, bool aAllowOpacity )
{
    COLOR4D result = COLOR4D::WHITE;

    if( isConcreteColor( aColor ) )
        result = aColor;
    else if( isConcreteColor( aDefault ) )
        result = aDefault;

    if( !aAllowOpacity )
        result.a = 1.0;

    return result;
}


wxColour compositeOn( const COLOR4D& aColor, const wxColour& aBackground )
{
    double alpha = std::clamp( aColor.a, 0.0, 1.0 );
    int red = std::lround( alpha * toByte( aColor.r ) + ( 1.0 - alpha ) * aBackground.Red() );
    int green = std::lround( alpha * toByte( aColor.g ) + ( 1.0 - alpha ) * aBackground.Green() );
    int blue = std::lround( alpha * toByte( aColor.b ) + ( 1.0 - alpha ) * aBackground.Blue() );
    return wxColour( red, green, blue );
}


void setPixel( unsigned char* aData, int aWidth, int aX, int aY, const COLOR4D& aColor )
{
    unsigned char* pixel = aData + ( aY * aWidth + aX ) * 3;
    pixel[0] = static_cast<unsigned char>( toByte( aColor.r ) );
    pixel[1] = static_cast<unsigned char>( toByte( aColor.g ) );
    pixel[2] = static_cast<unsigned char>( toByte( aColor.b ) );
}


struct NAMED_COLOR
{
    COLOR4D color;
    wxString name;
};


std::vector<NAMED_COLOR>& recentColors()
{
    // Recent colors intentionally live for the process lifetime, matching the lightweight Qt
    // prototype without adding a settings migration or disk write to every color change.
    static std::vector<NAMED_COLOR> colors;
    return colors;
}


std::vector<NAMED_COLOR> fixedPresetColors()
{
    // A compact, high-contrast subset of the legacy Defined Colors palette.  Closely spaced
    // intermediate shades are intentionally omitted so every preset remains useful at swatch
    // size and the row exactly matches the twenty recent-color slots below it.
    static constexpr std::array<EDA_COLOR_T, MAX_RECENT_COLORS> presets = {
        BLACK,       DARKGRAY,   LIGHTGRAY,   WHITE,
        DARKBLUE,    DARKGREEN,  DARKCYAN,    DARKRED,      DARKMAGENTA, DARKORANGE,
        BLUE,        GREEN,      CYAN,        RED,          MAGENTA,     YELLOW,
        PUREBLUE,    PUREGREEN,  PURERED,     PUREORANGE
    };

    std::vector<NAMED_COLOR> colors;
    colors.reserve( presets.size() );

    for( EDA_COLOR_T colorId : presets )
    {
        for( int index = 0; index < NBCOLORS; ++index )
        {
            const StructColors& preset = colorRefs()[index];

            if( preset.m_Numcolor == colorId )
            {
                colors.push_back( { COLOR4D( colorId ),
                                    wxGetTranslation(
                                            wxString::FromUTF8( preset.m_ColorName ) ) } );
                break;
            }
        }
    }

    return colors;
}


class COLOR_SWATCH_GRID : public wxPanel
{
public:
    using SELECT_CALLBACK = std::function<void( const COLOR4D& )>;

    COLOR_SWATCH_GRID( wxWindow* aParent, SELECT_CALLBACK aCallback ) :
            wxPanel( aParent, wxID_ANY ),
            m_callback( std::move( aCallback ) ),
            m_hoverSlot( -1 )
    {
        SetBackgroundStyle( wxBG_STYLE_PAINT );
        wxSize gridSize = FromDIP( wxSize( 420, 42 ) );
        SetMinSize( gridSize );
        SetMaxSize( gridSize );

        Bind( wxEVT_PAINT, &COLOR_SWATCH_GRID::onPaint, this );
        Bind( wxEVT_LEFT_UP, &COLOR_SWATCH_GRID::onLeftUp, this );
        Bind( wxEVT_MOTION, &COLOR_SWATCH_GRID::onMotion, this );
        Bind( wxEVT_LEAVE_WINDOW, &COLOR_SWATCH_GRID::onLeave, this );
    }

    void SetPresetColors( const std::vector<NAMED_COLOR>& aColors )
    {
        assignRow( m_presets, aColors );
        Refresh( false );
    }

    void SetRecentColors( const std::vector<NAMED_COLOR>& aColors )
    {
        assignRow( m_recent, aColors );
        Refresh( false );
    }

    void ClearCallback()
    {
        m_callback = nullptr;
    }

private:
    static void assignRow( std::vector<NAMED_COLOR>& aDestination,
                           const std::vector<NAMED_COLOR>& aSource )
    {
        aDestination.assign(
                aSource.begin(),
                aSource.begin() + std::min( aSource.size(), MAX_RECENT_COLORS ) );
    }

    wxRect swatchRect( int aRow, int aColumn ) const
    {
        int gap = FromDIP( 2 );
        wxSize size = GetClientSize();
        int swatch = std::max( ( size.x - 19 * gap ) / 20, 1 );
        int gridWidth = 20 * swatch + 19 * gap;
        int gridHeight = 2 * swatch + gap;
        int left = std::max( ( size.x - gridWidth ) / 2, 0 );
        int top = std::max( ( size.y - gridHeight ) / 2, 0 );
        return wxRect( left + aColumn * ( swatch + gap ),
                       top + aRow * ( swatch + gap ), swatch, swatch );
    }

    const NAMED_COLOR* colorAt( int aSlot ) const
    {
        if( aSlot < 0 )
            return nullptr;

        int row = aSlot / static_cast<int>( MAX_RECENT_COLORS );
        size_t column = static_cast<size_t>( aSlot ) % MAX_RECENT_COLORS;
        const std::vector<NAMED_COLOR>& colors = row == 0 ? m_presets : m_recent;
        return column < colors.size() ? &colors[column] : nullptr;
    }

    int hitTest( const wxPoint& aPosition ) const
    {
        for( int row = 0; row < 2; ++row )
        {
            for( int column = 0; column < static_cast<int>( MAX_RECENT_COLORS ); ++column )
            {
                if( swatchRect( row, column ).Contains( aPosition ) )
                    return row * static_cast<int>( MAX_RECENT_COLORS ) + column;
            }
        }

        return -1;
    }

    void onPaint( wxPaintEvent& )
    {
        wxAutoBufferedPaintDC dc( this );
        dc.SetBackground( wxBrush( GetBackgroundColour() ) );
        dc.Clear();

        int checker = std::max( FromDIP( 3 ), 1 );

        for( int row = 0; row < 2; ++row )
        {
            for( int column = 0; column < static_cast<int>( MAX_RECENT_COLORS ); ++column )
            {
                int slot = row * static_cast<int>( MAX_RECENT_COLORS ) + column;
                wxRect rect = swatchRect( row, column );
                const NAMED_COLOR* item = colorAt( slot );

                if( item )
                {
                    for( int y = rect.y; y < rect.y + rect.height; y += checker )
                    {
                        for( int x = rect.x; x < rect.x + rect.width; x += checker )
                        {
                            bool light = ( ( x - rect.x ) / checker + ( y - rect.y ) / checker )
                                         % 2 == 0;
                            wxColour background = light ? wxColour( 235, 235, 235 )
                                                        : wxColour( 185, 185, 185 );
                            wxRect tile( x, y, std::min( checker, rect.x + rect.width - x ),
                                        std::min( checker, rect.y + rect.height - y ) );
                            dc.SetPen( *wxTRANSPARENT_PEN );
                            dc.SetBrush( wxBrush( compositeOn( item->color, background ) ) );
                            dc.DrawRectangle( tile );
                        }
                    }
                }
                else
                {
                    dc.SetPen( *wxTRANSPARENT_PEN );
                    dc.SetBrush( wxBrush( wxSystemSettings::GetColour( wxSYS_COLOUR_BTNFACE ) ) );
                    dc.DrawRectangle( rect );
                }

                dc.SetBrush( *wxTRANSPARENT_BRUSH );
                dc.SetPen( wxPen( slot == m_hoverSlot && item
                                          ? wxSystemSettings::GetColour( wxSYS_COLOUR_HIGHLIGHT )
                                          : wxSystemSettings::GetColour(
                                                    wxSYS_COLOUR_BTNSHADOW ) ) );
                dc.DrawRectangle( rect );
            }
        }
    }

    void onLeftUp( wxMouseEvent& aEvent )
    {
        int slot = hitTest( aEvent.GetPosition() );
        const NAMED_COLOR* item = colorAt( slot );

        if( item && m_callback )
            m_callback( item->color );
    }

    void onMotion( wxMouseEvent& aEvent )
    {
        int slot = hitTest( aEvent.GetPosition() );
        const NAMED_COLOR* item = colorAt( slot );

        if( slot == m_hoverSlot )
            return;

        m_hoverSlot = slot;

        if( item )
        {
            wxString tooltip = item->name;

            if( tooltip.IsEmpty() )
                tooltip = item->color.ToHexString();

            SetToolTip( tooltip );
        }
        else
        {
            UnsetToolTip();
        }

        Refresh( false );
    }

    void onLeave( wxMouseEvent& )
    {
        m_hoverSlot = -1;
        UnsetToolTip();
        Refresh( false );
    }

private:
    std::vector<NAMED_COLOR> m_presets;
    std::vector<NAMED_COLOR> m_recent;
    SELECT_CALLBACK          m_callback;
    int                      m_hoverSlot;
};


class COLOR_PICKER_CANVAS : public wxPanel
{
public:
    using CHANGE_CALLBACK = std::function<void( const COLOR4D&, bool )>;

    COLOR_PICKER_CANVAS( wxWindow* aParent, bool aAllowOpacity, CHANGE_CALLBACK aCallback ) :
            wxPanel( aParent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_SIMPLE ),
            m_allowOpacity( aAllowOpacity ),
            m_callback( std::move( aCallback ) ),
            m_dragArea( DRAG_AREA::NONE ),
            m_hue( 0.0 ),
            m_saturation( 1.0 ),
            m_value( 1.0 ),
            m_alpha( 1.0 ),
            m_svHue( -1.0 )
    {
        SetBackgroundStyle( wxBG_STYLE_PAINT );
        SetMinSize( FromDIP( wxSize( 420, 205 ) ) );

        Bind( wxEVT_PAINT, &COLOR_PICKER_CANVAS::onPaint, this );
        Bind( wxEVT_SIZE, &COLOR_PICKER_CANVAS::onSize, this );
        Bind( wxEVT_LEFT_DOWN, &COLOR_PICKER_CANVAS::onLeftDown, this );
        Bind( wxEVT_LEFT_UP, &COLOR_PICKER_CANVAS::onLeftUp, this );
        Bind( wxEVT_MOTION, &COLOR_PICKER_CANVAS::onMotion, this );
        Bind( wxEVT_MOUSE_CAPTURE_LOST, &COLOR_PICKER_CANVAS::onCaptureLost, this );
        Bind( wxEVT_MOUSEWHEEL, &COLOR_PICKER_CANVAS::onMouseWheel, this );
    }

    void SetColor( const COLOR4D& aColor )
    {
        double hue = 0.0;
        double saturation = 0.0;
        double value = 0.0;
        aColor.ToHSV( hue, saturation, value, true );

        if( saturation > 0.000001 && std::abs( hue - m_hue ) > 0.000001 )
        {
            m_hue = hue;
            m_svBitmap = wxBitmap();
        }

        m_saturation = std::clamp( saturation, 0.0, 1.0 );
        m_value = std::clamp( value, 0.0, 1.0 );
        m_alpha = m_allowOpacity ? std::clamp( aColor.a, 0.0, 1.0 ) : 1.0;
        m_alphaBitmap = wxBitmap();
        Refresh( false );
    }

    COLOR4D GetColor() const
    {
        return fromHSV( m_hue, m_saturation, m_value, m_alpha );
    }

    void ClearCallback()
    {
        m_callback = nullptr;
    }

private:
    enum class DRAG_AREA
    {
        NONE,
        SATURATION_VALUE,
        ALPHA,
        HUE
    };

    wxRect colorRect() const
    {
        wxSize size = GetClientSize();
        int margin = FromDIP( 8 );
        int gap = FromDIP( 10 );
        int barWidth = FromDIP( 18 );
        int bars = m_allowOpacity ? 2 : 1;
        int width = size.x - 2 * margin - bars * barWidth - bars * gap;
        return wxRect( margin, margin, std::max( width, 1 ), std::max( size.y - 2 * margin, 1 ) );
    }

    wxRect alphaRect() const
    {
        if( !m_allowOpacity )
            return wxRect();

        wxRect color = colorRect();
        return wxRect( color.GetRight() + 1 + FromDIP( 10 ), color.y, FromDIP( 18 ), color.height );
    }

    wxRect hueRect() const
    {
        wxRect color = colorRect();
        int x = color.GetRight() + 1 + FromDIP( 10 );

        if( m_allowOpacity )
            x += FromDIP( 18 ) + FromDIP( 10 );

        return wxRect( x, color.y, FromDIP( 18 ), color.height );
    }

    static double normalizedX( const wxRect& aRect, int aX )
    {
        int x = std::clamp( aX, aRect.x, aRect.GetRight() );
        return aRect.width > 1 ? static_cast<double>( x - aRect.x ) / ( aRect.width - 1 ) : 0.0;
    }

    static double normalizedY( const wxRect& aRect, int aY )
    {
        int y = std::clamp( aY, aRect.y, aRect.GetBottom() );
        return aRect.height > 1 ? static_cast<double>( y - aRect.y ) / ( aRect.height - 1 ) : 0.0;
    }

    void ensureBitmaps()
    {
        wxRect color = colorRect();
        wxRect hue = hueRect();
        wxRect alpha = alphaRect();

        if( !m_svBitmap.IsOk() || m_svBitmap.GetSize() != color.GetSize()
                || std::abs( m_svHue - m_hue ) > 0.000001 )
        {
            wxImage image( color.width, color.height );
            unsigned char* data = image.GetData();

            for( int y = 0; y < color.height; ++y )
            {
                double value = color.height > 1
                                       ? 1.0 - static_cast<double>( y ) / ( color.height - 1 )
                                       : 1.0;

                for( int x = 0; x < color.width; ++x )
                {
                    double saturation = color.width > 1
                                                ? static_cast<double>( x ) / ( color.width - 1 )
                                                : 0.0;
                    setPixel( data, color.width, x, y,
                              fromHSV( m_hue, saturation, value ) );
                }
            }

            m_svBitmap = wxBitmap( image );
            m_svHue = m_hue;
        }

        if( !m_hueBitmap.IsOk() || m_hueBitmap.GetSize() != hue.GetSize() )
        {
            wxImage image( hue.width, hue.height );
            unsigned char* data = image.GetData();

            for( int y = 0; y < hue.height; ++y )
            {
                double value = hue.height > 1
                                       ? 1.0 - static_cast<double>( y ) / ( hue.height - 1 )
                                       : 1.0;
                COLOR4D colorAtY = fromHSV( value * 360.0, 1.0, 1.0 );

                for( int x = 0; x < hue.width; ++x )
                    setPixel( data, hue.width, x, y, colorAtY );
            }

            m_hueBitmap = wxBitmap( image );
        }

        if( m_allowOpacity
                && ( !m_alphaBitmap.IsOk() || m_alphaBitmap.GetSize() != alpha.GetSize() ) )
        {
            wxImage image( alpha.width, alpha.height );
            unsigned char* data = image.GetData();
            COLOR4D base = fromHSV( m_hue, m_saturation, m_value );
            int checker = std::max( FromDIP( 4 ), 1 );

            for( int y = 0; y < alpha.height; ++y )
            {
                double opacity = alpha.height > 1
                                         ? 1.0 - static_cast<double>( y ) / ( alpha.height - 1 )
                                         : 1.0;

                for( int x = 0; x < alpha.width; ++x )
                {
                    bool light = ( x / checker + y / checker ) % 2 == 0;
                    double background = light ? 0.92 : 0.68;
                    COLOR4D mixed( opacity * base.r + ( 1.0 - opacity ) * background,
                                   opacity * base.g + ( 1.0 - opacity ) * background,
                                   opacity * base.b + ( 1.0 - opacity ) * background, 1.0 );
                    setPixel( data, alpha.width, x, y, mixed );
                }
            }

            m_alphaBitmap = wxBitmap( image );
        }
    }

    void updateFromPosition( const wxPoint& aPosition, bool aFinal )
    {
        switch( m_dragArea )
        {
        case DRAG_AREA::SATURATION_VALUE:
            m_saturation = normalizedX( colorRect(), aPosition.x );
            m_value = 1.0 - normalizedY( colorRect(), aPosition.y );
            m_alphaBitmap = wxBitmap();
            break;

        case DRAG_AREA::ALPHA:
            m_alpha = 1.0 - normalizedY( alphaRect(), aPosition.y );
            break;

        case DRAG_AREA::HUE:
            m_hue = ( 1.0 - normalizedY( hueRect(), aPosition.y ) ) * 360.0;
            m_svBitmap = wxBitmap();
            m_alphaBitmap = wxBitmap();
            break;

        case DRAG_AREA::NONE:
            return;
        }

        Refresh( false );

        if( m_callback )
            m_callback( GetColor(), aFinal );
    }

    void onPaint( wxPaintEvent& )
    {
        wxAutoBufferedPaintDC dc( this );
        dc.SetBackground( wxBrush( GetBackgroundColour() ) );
        dc.Clear();
        ensureBitmaps();

        wxRect color = colorRect();
        wxRect alpha = alphaRect();
        wxRect hue = hueRect();
        dc.DrawBitmap( m_svBitmap, color.GetPosition() );

        if( m_allowOpacity )
            dc.DrawBitmap( m_alphaBitmap, alpha.GetPosition() );

        dc.DrawBitmap( m_hueBitmap, hue.GetPosition() );

        int sx = color.x + std::lround( m_saturation * ( color.width - 1 ) );
        int sy = color.y + std::lround( ( 1.0 - m_value ) * ( color.height - 1 ) );
        int radius = FromDIP( 5 );
        dc.SetBrush( *wxTRANSPARENT_BRUSH );
        dc.SetPen( wxPen( *wxWHITE, std::max( FromDIP( 2 ), 1 ) ) );
        dc.DrawCircle( sx, sy, radius + FromDIP( 1 ) );
        dc.SetPen( wxPen( *wxBLACK, std::max( FromDIP( 1 ), 1 ) ) );
        dc.DrawCircle( sx, sy, radius );

        auto drawBarMarker = [&]( const wxRect& aRect, double aNormalized )
        {
            int y = aRect.y + std::lround( ( 1.0 - aNormalized ) * ( aRect.height - 1 ) );
            wxRect marker( aRect.x - FromDIP( 3 ), y - FromDIP( 2 ),
                           aRect.width + FromDIP( 6 ), FromDIP( 5 ) );
            dc.SetBrush( *wxTRANSPARENT_BRUSH );
            dc.SetPen( wxPen( *wxWHITE, std::max( FromDIP( 2 ), 1 ) ) );
            dc.DrawRectangle( marker );
            marker.Deflate( FromDIP( 1 ) );
            dc.SetPen( wxPen( *wxBLACK, std::max( FromDIP( 1 ), 1 ) ) );
            dc.DrawRectangle( marker );
        };

        if( m_allowOpacity )
            drawBarMarker( alpha, m_alpha );

        drawBarMarker( hue, m_hue / 360.0 );
    }

    void onSize( wxSizeEvent& aEvent )
    {
        m_svBitmap = wxBitmap();
        m_hueBitmap = wxBitmap();
        m_alphaBitmap = wxBitmap();
        Refresh( false );
        aEvent.Skip();
    }

    void onLeftDown( wxMouseEvent& aEvent )
    {
        wxPoint position = aEvent.GetPosition();

        if( colorRect().Contains( position ) )
            m_dragArea = DRAG_AREA::SATURATION_VALUE;
        else if( m_allowOpacity && alphaRect().Contains( position ) )
            m_dragArea = DRAG_AREA::ALPHA;
        else if( hueRect().Contains( position ) )
            m_dragArea = DRAG_AREA::HUE;
        else
            return;

        SetFocus();
        CaptureMouse();
        updateFromPosition( position, false );
    }

    void onLeftUp( wxMouseEvent& aEvent )
    {
        if( m_dragArea == DRAG_AREA::NONE )
            return;

        updateFromPosition( aEvent.GetPosition(), true );
        m_dragArea = DRAG_AREA::NONE;

        if( HasCapture() )
            ReleaseMouse();
    }

    void onMotion( wxMouseEvent& aEvent )
    {
        if( m_dragArea != DRAG_AREA::NONE && aEvent.Dragging() && aEvent.LeftIsDown() )
            updateFromPosition( aEvent.GetPosition(), false );
    }

    void onCaptureLost( wxMouseCaptureLostEvent& )
    {
        m_dragArea = DRAG_AREA::NONE;
    }

    void onMouseWheel( wxMouseEvent& aEvent )
    {
        int direction = aEvent.GetWheelRotation() > 0 ? 1 : -1;

        if( hueRect().Contains( aEvent.GetPosition() ) )
        {
            m_hue = std::fmod( m_hue + direction + 360.0, 360.0 );
            m_svBitmap = wxBitmap();
            m_alphaBitmap = wxBitmap();
        }
        else if( m_allowOpacity && alphaRect().Contains( aEvent.GetPosition() ) )
        {
            m_alpha = std::clamp( m_alpha + direction * 0.01, 0.0, 1.0 );
        }
        else
        {
            aEvent.Skip();
            return;
        }

        Refresh( false );

        if( m_callback )
            m_callback( GetColor(), true );
    }

private:
    bool             m_allowOpacity;
    CHANGE_CALLBACK  m_callback;
    DRAG_AREA        m_dragArea;
    double           m_hue;
    double           m_saturation;
    double           m_value;
    double           m_alpha;
    wxBitmap         m_svBitmap;
    wxBitmap         m_hueBitmap;
    wxBitmap         m_alphaBitmap;
    double           m_svHue;
};

} // namespace


struct DIALOG_COLOR_PICKER::IMPL
{
    enum class MODE
    {
        ARGB,
        CMYK,
        HSV,
        HSL
    };

    enum class SCREEN_PICKER_AVAILABILITY
    {
        UNKNOWN,
        AVAILABLE,
        UNAVAILABLE
    };

    explicit IMPL( DIALOG_COLOR_PICKER* aDialog, const COLOR4D& aCurrentColor,
                   bool aAllowOpacity, std::vector<CUSTOM_COLOR_ITEM>* aUserColors,
                   const COLOR4D& aDefaultColor ) :
            dialog( aDialog ),
            allowOpacity( aAllowOpacity ),
            color( aCurrentColor ),
            defaultColor( aDefaultColor ),
            displayColor( editableColor( aCurrentColor, aDefaultColor, aAllowOpacity ) ),
            mode( MODE::ARGB ),
            updatingControls( false ),
            canvas( nullptr ),
            swatchGrid( nullptr ),
            modeButton( nullptr ),
            hexInput( nullptr ),
            resetButton( nullptr ),
            okButton( nullptr ),
            eyedropperTimer( aDialog ),
            previousLeftDown( false ),
            previousRightDown( false ),
            eyedropperCursorSet( false ),
            screenPickerAvailability( SCREEN_PICKER_AVAILABILITY::UNKNOWN ),
            finishing( false ),
            interactionReady( false ),
            shutdownComplete( false )
    {
        if( !allowOpacity && isConcreteColor( color ) )
            color.a = 1.0;

        if( aUserColors )
        {
            for( const CUSTOM_COLOR_ITEM& item : *aUserColors )
            {
                if( recent.size() == MAX_RECENT_COLORS )
                    break;

                if( isConcreteColor( item.m_Color ) )
                    recent.push_back( { item.m_Color, item.m_ColorName } );
            }
        }
        else
        {
            recent = recentColors();

            if( isConcreteColor( color ) )
                addRecent( color, false );
        }

        buildControls();
        setColor( color, false );
    }

    ~IMPL()
    {
        shutdown();
    }

    void buildControls()
    {
        auto* mainSizer = new wxBoxSizer( wxVERTICAL );
        dialog->SetSizer( mainSizer );

        swatchGrid = new COLOR_SWATCH_GRID(
                dialog,
                [this]( const COLOR4D& aColor )
                {
                    acceptSwatchColor( aColor );
                } );
        swatchGrid->SetPresetColors( fixedPresetColors() );
        swatchGrid->SetRecentColors( recent );
        mainSizer->Add( swatchGrid, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP,
                        dialog->FromDIP( 8 ) );

        canvas = new COLOR_PICKER_CANVAS(
                dialog, allowOpacity,
                [this]( const COLOR4D& aColor, bool aFinal )
                {
                    color = aColor;
                    displayColor = aColor;
                    updateControls();

                    if( aFinal )
                        addRecent( aColor, true );
                } );
        mainSizer->Add( canvas, 1, wxEXPAND | wxALL, dialog->FromDIP( 8 ) );

        auto* inputSizer = new wxBoxSizer( wxHORIZONTAL );
        modeButton = new wxButton( dialog, wxID_ANY, wxS( "ARGB" ) );
        modeButton->SetMinSize( dialog->FromDIP( wxSize( 54, -1 ) ) );
        inputSizer->Add( modeButton, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, dialog->FromDIP( 5 ) );

        hexInput = new wxTextCtrl( dialog, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                   dialog->FromDIP( wxSize( 92, -1 ) ), wxTE_PROCESS_ENTER );
        hexInput->SetHint( wxS( "#AARRGGBB" ) );
        hexInput->SetMaxLength( 64 );
        inputSizer->Add( hexInput, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, dialog->FromDIP( 7 ) );

        for( size_t i = 0; i < channelInputs.size(); ++i )
        {
            channelLabels[i] = new wxStaticText( dialog, wxID_ANY, wxEmptyString );
            channelLabels[i]->SetMinSize( dialog->FromDIP( wxSize( 15, -1 ) ) );
            channelLabels[i]->SetWindowStyleFlag( wxALIGN_RIGHT );
            inputSizer->Add( channelLabels[i], 0, wxALIGN_CENTER_VERTICAL | wxLEFT,
                             i == 0 ? 0 : dialog->FromDIP( 3 ) );

            channelInputs[i] = new wxTextCtrl( dialog, wxID_ANY, wxEmptyString,
                                               wxDefaultPosition,
                                               dialog->FromDIP( wxSize( 38, -1 ) ),
                                               wxTE_PROCESS_ENTER | wxTE_CENTRE );
            channelInputs[i]->SetMaxLength( 3 );
            inputSizer->Add( channelInputs[i], 0, wxALIGN_CENTER_VERTICAL );
        }

        mainSizer->Add( inputSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM,
                        dialog->FromDIP( 8 ) );

        auto* bottomSizer = new wxBoxSizer( wxHORIZONTAL );
        resetButton = new wxButton( dialog, wxID_RESET,
                                    defaultColor == COLOR4D::UNSPECIFIED
                                            ? _( "Clear Color" )
                                            : _( "Reset to Default" ) );
        bottomSizer->Add( resetButton, 0, wxALIGN_CENTER_VERTICAL );
        bottomSizer->AddStretchSpacer( 1 );

        auto* standardButtons = new wxStdDialogButtonSizer;
        okButton = new wxButton( dialog, wxID_OK );
        standardButtons->AddButton( okButton );
        standardButtons->AddButton( new wxButton( dialog, wxID_CANCEL ) );
        standardButtons->Realize();
        bottomSizer->Add( standardButtons, 0, wxALIGN_CENTER_VERTICAL );
        mainSizer->Add( bottomSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM,
                        dialog->FromDIP( 8 ) );

        dialog->SetupStandardButtons();
        okButton->SetDefault();
        dialog->SetAffirmativeId( wxID_OK );
        dialog->SetEscapeId( wxID_CANCEL );
        dialog->SetInitialFocus( canvas );

        modeButton->Bind( wxEVT_BUTTON, &IMPL::onModeButton, this );
        resetButton->Bind( wxEVT_BUTTON, &IMPL::onResetButton, this );
        okButton->Bind( wxEVT_BUTTON, &IMPL::onOkButton, this );

        auto bindEditor = [this]( wxTextCtrl* aControl )
        {
            aControl->Bind( wxEVT_KILL_FOCUS, &IMPL::onEditorKillFocus, this );
            aControl->Bind( wxEVT_MOUSEWHEEL, &IMPL::onEditorMouseWheel, this );
        };

        bindEditor( hexInput );

        for( wxTextCtrl* input : channelInputs )
            bindEditor( input );

        std::function<void( wxWindow* )> bindRightClick =
                [&]( wxWindow* aWindow )
                {
                    aWindow->Bind( wxEVT_RIGHT_DOWN, &IMPL::onRightClick, this );
                    rightClickWindows.push_back( aWindow );

                    for( wxWindow* child : aWindow->GetChildren() )
                        bindRightClick( child );
                };

        bindRightClick( dialog );

        dialog->Bind( wxEVT_TIMER, &IMPL::onTimer, this, eyedropperTimer.GetId() );

        // wxEVT_ACTIVATE catches clicks which are shorter than the polling interval.  It also
        // keeps external KiCad frames usable: no global mouse capture is needed.
        dialog->Bind( wxEVT_ACTIVATE, &IMPL::onActivate, this );
    }

    void onModeButton( wxCommandEvent& )
    {
        commitFocusedEditor();
        mode = static_cast<MODE>( ( static_cast<int>( mode ) + 1 ) % 4 );
        updateControls();
    }

    void onResetButton( wxCommandEvent& )
    {
        setColor( defaultColor, true );
    }

    void onOkButton( wxCommandEvent& aEvent )
    {
        if( commitFocusedEditor() )
        {
            addRecent( color, true );
            aEvent.Skip();
        }
    }

    void onEditorKillFocus( wxFocusEvent& aEvent )
    {
        if( !updatingControls )
            commitFocusedEditor();

        aEvent.Skip();
    }

    void onEditorMouseWheel( wxMouseEvent& aEvent )
    {
        wxTextCtrl* control = dynamic_cast<wxTextCtrl*>( aEvent.GetEventObject() );

        if( !control || control == hexInput || !control->IsEnabled() )
        {
            aEvent.Skip();
            return;
        }

        size_t index = 0;

        while( index < channelInputs.size() && channelInputs[index] != control )
            ++index;

        if( index == channelInputs.size() )
        {
            aEvent.Skip();
            return;
        }

        long value = 0;
        control->GetValue().ToLong( &value );
        int direction = aEvent.GetWheelRotation() > 0 ? 1 : -1;
        value = std::clamp<long>( value + direction, channelMinimum[index],
                                 channelMaximum[index] );
        control->ChangeValue( wxString::Format( wxS( "%ld" ), value ) );
        commitChannels();
    }

    void onRightClick( wxMouseEvent& )
    {
        if( interactionReady )
            cancelPicker();
    }

    void onTimer( wxTimerEvent& )
    {
        pollOutsideClick();
    }

    void onActivate( wxActivateEvent& aEvent )
    {
        if( interactionReady && !aEvent.GetActive() && !finishing
                && !pointInsideDialog( wxGetMousePosition() ) )
        {
            wxMouseState state = wxGetMouseState();

            // Activation can also be lost to a screenshot shortcut, a window-manager action, or
            // another application.  Only a real mouse press is an outside-click confirmation.
            if( state.RightIsDown() )
                cancelPicker();
            else if( state.LeftIsDown() )
                acceptOutsideColor();
        }

        aEvent.Skip();
    }

    void setColor( const COLOR4D& aColor, bool aAddRecent )
    {
        color = aColor;

        if( !allowOpacity && isConcreteColor( color ) )
            color.a = 1.0;

        displayColor = editableColor( color, defaultColor, allowOpacity );
        canvas->SetColor( displayColor );
        updateControls();

        if( aAddRecent )
            addRecent( color, true );
    }

    void updateControls()
    {
        if( updatingControls )
            return;

        updatingControls = true;

        if( color.m_text )
        {
            hexInput->ChangeValue( *color.m_text );
        }
        else if( color == COLOR4D::UNSPECIFIED )
        {
            hexInput->ChangeValue( wxEmptyString );
        }
        else
        {
            hexInput->ChangeValue( wxString::Format( wxS( "#%02X%02X%02X%02X" ),
                                                     toByte( color.a ), toByte( color.r ),
                                                     toByte( color.g ), toByte( color.b ) ) );
        }

        auto configure = [&]( size_t aIndex, const wxString& aLabel, int aValue,
                              bool aEnabled, int aMaximum )
        {
            channelLabels[aIndex]->SetLabel( aLabel );
            channelInputs[aIndex]->Enable( aEnabled );
            channelInputs[aIndex]->SetHint( aEnabled ? wxString::Format( wxS( "0-%d" ), aMaximum )
                                                      : wxS( "-" ) );
            channelInputs[aIndex]->ChangeValue( aEnabled
                                                        ? wxString::Format( wxS( "%d" ), aValue )
                                                        : wxString() );
            channelMinimum[aIndex] = 0;
            channelMaximum[aIndex] = aMaximum;
        };

        switch( mode )
        {
        case MODE::ARGB:
            modeButton->SetLabel( allowOpacity ? wxS( "ARGB" ) : wxS( "RGB" ) );
            configure( 0, allowOpacity ? wxS( "A:" ) : wxEmptyString, toByte( displayColor.a ),
                       allowOpacity, 255 );
            configure( 1, wxS( "R:" ), toByte( displayColor.r ), true, 255 );
            configure( 2, wxS( "G:" ), toByte( displayColor.g ), true, 255 );
            configure( 3, wxS( "B:" ), toByte( displayColor.b ), true, 255 );
            break;

        case MODE::CMYK:
        {
            modeButton->SetLabel( wxS( "CMYK" ) );
            double black = 1.0 - std::max( { displayColor.r, displayColor.g, displayColor.b } );
            double denominator = 1.0 - black;
            double cyan = denominator > 0.000001 ? ( 1.0 - displayColor.r - black ) / denominator : 0.0;
            double magenta = denominator > 0.000001 ? ( 1.0 - displayColor.g - black ) / denominator : 0.0;
            double yellow = denominator > 0.000001 ? ( 1.0 - displayColor.b - black ) / denominator : 0.0;
            configure( 0, wxS( "K:" ), toByte( black ), true, 255 );
            configure( 1, wxS( "C:" ), toByte( cyan ), true, 255 );
            configure( 2, wxS( "M:" ), toByte( magenta ), true, 255 );
            configure( 3, wxS( "Y:" ), toByte( yellow ), true, 255 );
            break;
        }

        case MODE::HSV:
        {
            modeButton->SetLabel( wxS( "HSV" ) );
            double hue = 0.0;
            double saturation = 0.0;
            double value = 0.0;
            displayColor.ToHSV( hue, saturation, value, true );
            configure( 0, wxEmptyString, 0, false, 100 );
            configure( 1, wxS( "H:" ), std::lround( hue ), true, 360 );
            configure( 2, wxS( "S:" ), std::lround( saturation * 100.0 ), true, 100 );
            configure( 3, wxS( "V:" ), std::lround( value * 100.0 ), true, 100 );
            break;
        }

        case MODE::HSL:
        {
            modeButton->SetLabel( wxS( "HSL" ) );
            double hue = 0.0;
            double saturation = 0.0;
            double lightness = 0.0;
            displayColor.ToHSL( hue, saturation, lightness );
            configure( 0, wxEmptyString, 0, false, 100 );
            configure( 1, wxS( "H:" ), std::lround( hue ), true, 360 );
            configure( 2, wxS( "S:" ), std::lround( saturation * 100.0 ), true, 100 );
            configure( 3, wxS( "L:" ), std::lround( lightness * 100.0 ), true, 100 );
            break;
        }
        }

        wxColour buttonColor = compositeOn( displayColor,
                                             wxSystemSettings::GetColour( wxSYS_COLOUR_BTNFACE ) );
        int luminance = ( 299 * buttonColor.Red() + 587 * buttonColor.Green()
                          + 114 * buttonColor.Blue() ) / 1000;
        okButton->SetBackgroundColour( buttonColor );
        okButton->SetForegroundColour( luminance < 128 ? *wxWHITE : *wxBLACK );
        okButton->Refresh();
        updatingControls = false;
    }

    bool commitFocusedEditor()
    {
        wxWindow* focus = wxWindow::FindFocus();

        if( focus == hexInput )
            return commitHex();

        for( wxTextCtrl* input : channelInputs )
        {
            if( focus == input )
                return commitChannels();
        }

        return true;
    }

    bool commitHex()
    {
        if( updatingControls )
            return true;

        wxString text = hexInput->GetValue();
        text.Trim( true ).Trim( false );

        if( text.IsEmpty() )
        {
            setColor( COLOR4D::UNSPECIFIED, false );
            return true;
        }

        wxString digits = text.StartsWith( wxS( "#" ) ) ? text.Mid( 1 ) : text;
        bool validHex = digits.length() == 6 || digits.length() == 8;

        validHex = validHex
                   && digits.find_first_not_of( wxS( "0123456789abcdefABCDEF" ) ) == wxString::npos;

        if( validHex )
        {
            auto byteAt = [&]( size_t aOffset )
            {
                unsigned long value = 0;
                digits.Mid( aOffset, 2 ).ToULong( &value, 16 );
                return static_cast<int>( value );
            };

            int alpha = allowOpacity ? toByte( displayColor.a ) : 255;
            int red = 0;
            int green = 0;
            int blue = 0;

            if( digits.length() == 8 )
            {
                alpha = allowOpacity ? byteAt( 0 ) : 255;
                red = byteAt( 2 );
                green = byteAt( 4 );
                blue = byteAt( 6 );
            }
            else
            {
                red = byteAt( 0 );
                green = byteAt( 2 );
                blue = byteAt( 4 );
            }

            setColor( COLOR4D( red / 255.0, green / 255.0, blue / 255.0, alpha / 255.0 ),
                      true );
            return true;
        }

        COLOR4D parsed;

        if( parsed.SetFromWxString( text ) )
        {
            if( !allowOpacity )
                parsed.a = 1.0;

            setColor( parsed, true );
            return true;
        }

        // KiCad color settings may contain symbolic expressions such as
        // @{color(DEVICE_BACKGROUND)}.  Keep those expressions intact while retaining the last
        // concrete color as an editing preview.
        color = displayColor;
        color.m_text = std::make_shared<wxString>( text );
        updateControls();
        return true;
    }

    bool commitChannels()
    {
        if( updatingControls )
            return true;

        std::array<int, 4> values{};

        for( size_t i = 0; i < channelInputs.size(); ++i )
        {
            if( !channelInputs[i]->IsEnabled() )
                continue;

            long value = channelMaximum[i];

            if( !channelInputs[i]->GetValue().ToLong( &value ) )
                value = channelMaximum[i];

            values[i] = std::clamp<long>( value, channelMinimum[i], channelMaximum[i] );
        }

        COLOR4D updated = displayColor;

        switch( mode )
        {
        case MODE::ARGB:
            updated = COLOR4D( values[1] / 255.0, values[2] / 255.0, values[3] / 255.0,
                               allowOpacity ? values[0] / 255.0 : 1.0 );
            break;

        case MODE::CMYK:
        {
            double black = values[0] / 255.0;
            double cyan = values[1] / 255.0;
            double magenta = values[2] / 255.0;
            double yellow = values[3] / 255.0;
            updated = COLOR4D( ( 1.0 - cyan ) * ( 1.0 - black ),
                               ( 1.0 - magenta ) * ( 1.0 - black ),
                               ( 1.0 - yellow ) * ( 1.0 - black ), displayColor.a );
            break;
        }

        case MODE::HSV:
            updated = fromHSV( values[1], values[2] / 100.0, values[3] / 100.0,
                               displayColor.a );
            break;

        case MODE::HSL:
            updated.FromHSL( values[1], values[2] / 100.0, values[3] / 100.0 );
            updated.a = displayColor.a;
            break;
        }

        setColor( updated, true );
        return true;
    }

    void addRecent( const COLOR4D& aColor, bool aUpdatePanel )
    {
        if( !isConcreteColor( aColor ) )
            return;

        auto addTo = [&]( std::vector<NAMED_COLOR>& aColors )
        {
            std::erase_if( aColors,
                           [&]( const NAMED_COLOR& aItem )
                           {
                               return sameConcreteColor( aItem.color, aColor );
                           } );
            aColors.insert( aColors.begin(), { aColor, wxEmptyString } );

            if( aColors.size() > MAX_RECENT_COLORS )
                aColors.resize( MAX_RECENT_COLORS );
        };

        addTo( recent );
        addTo( recentColors() );

        if( aUpdatePanel && swatchGrid )
            swatchGrid->SetRecentColors( recent );
    }

    void positionNearCursor()
    {
        wxPoint cursor = wxGetMousePosition();
        int displayIndex = wxDisplay::GetFromPoint( cursor );

        if( displayIndex == wxNOT_FOUND )
            displayIndex = 0;

        wxRect workArea = wxDisplay( static_cast<unsigned int>( displayIndex ) ).GetClientArea();
        wxSize size = dialog->GetSize();
        wxPoint position( cursor.x - size.x / 2, cursor.y - dialog->FromDIP( 22 ) );
        position.x = std::clamp( position.x, workArea.x,
                                 std::max( workArea.x, workArea.GetRight() - size.x + 1 ) );
        position.y = std::clamp( position.y, workArea.y,
                                 std::max( workArea.y, workArea.GetBottom() - size.y + 1 ) );
        dialog->SetPosition( position );
    }

    void shutdown()
    {
        if( shutdownComplete )
            return;

        shutdownComplete = true;
        stopOutsideMonitor();

        // DIALOG_COLOR_PICKER owns its wx children through the base class, so they outlive this
        // implementation object.  Remove every handler and callback that refers back to IMPL
        // before its storage is released.
        if( swatchGrid )
            swatchGrid->ClearCallback();

        if( canvas )
            canvas->ClearCallback();

        if( modeButton )
            modeButton->Unbind( wxEVT_BUTTON, &IMPL::onModeButton, this );

        if( resetButton )
            resetButton->Unbind( wxEVT_BUTTON, &IMPL::onResetButton, this );

        if( okButton )
            okButton->Unbind( wxEVT_BUTTON, &IMPL::onOkButton, this );

        if( hexInput )
        {
            hexInput->Unbind( wxEVT_KILL_FOCUS, &IMPL::onEditorKillFocus, this );
            hexInput->Unbind( wxEVT_MOUSEWHEEL, &IMPL::onEditorMouseWheel, this );
        }

        for( wxTextCtrl* input : channelInputs )
        {
            if( input )
            {
                input->Unbind( wxEVT_KILL_FOCUS, &IMPL::onEditorKillFocus, this );
                input->Unbind( wxEVT_MOUSEWHEEL, &IMPL::onEditorMouseWheel, this );
            }
        }

        for( wxWindow* window : rightClickWindows )
            window->Unbind( wxEVT_RIGHT_DOWN, &IMPL::onRightClick, this );

        rightClickWindows.clear();
        dialog->Unbind( wxEVT_TIMER, &IMPL::onTimer, this, eyedropperTimer.GetId() );
        dialog->Unbind( wxEVT_ACTIVATE, &IMPL::onActivate, this );
    }

    void startOutsideMonitor()
    {
        wxMouseState state = wxGetMouseState();
        previousLeftDown = state.LeftIsDown();
        previousRightDown = state.RightIsDown();
        eyedropperCursorSet = false;
        screenPickerAvailability = SCREEN_PICKER_AVAILABILITY::UNKNOWN;
        finishing = false;
        interactionReady = false;
        eyedropperTimer.Start( 25 );
    }

    void stopOutsideMonitor()
    {
        finishing = true;
        interactionReady = false;
        eyedropperTimer.Stop();
        setEyedropperCursor( false );
    }

    bool pointInsideDialog( const wxPoint& aScreenPoint ) const
    {
        wxRect bounds = dialog->GetScreenRect();

        // Include native decorations and resize shadows, which are not consistently reported by
        // GetScreenRect() on all wxWidgets ports.
        int side = dialog->FromDIP( 12 );
        int title = dialog->FromDIP( 32 );
        int shadow = dialog->FromDIP( 8 );
        bounds.x -= side;
        bounds.width += 2 * side;
        bounds.y -= title;
        bounds.height += title + shadow;
        return bounds.Contains( aScreenPoint );
    }

    void pollOutsideClick()
    {
        if( finishing )
            return;

        // ShowQuasiModal disables the parent before it marks its nested event loop active.  A
        // timer event can only arrive after that setup is complete, so it is the safe point from
        // which mouse and activation handlers may close this dialog.
        interactionReady = true;

        wxMouseState state = wxGetMouseState();
        bool leftDown = state.LeftIsDown();
        bool rightDown = state.RightIsDown();
        bool outside = !pointInsideDialog( wxGetMousePosition() );

        if( outside && screenPickerAvailability == SCREEN_PICKER_AVAILABILITY::UNKNOWN )
        {
            wxColour ignored;
            screenPickerAvailability = sampleScreenColor( ignored )
                                               ? SCREEN_PICKER_AVAILABILITY::AVAILABLE
                                               : SCREEN_PICKER_AVAILABILITY::UNAVAILABLE;
        }

        setEyedropperCursor( outside
                            && screenPickerAvailability == SCREEN_PICKER_AVAILABILITY::AVAILABLE );

        if( rightDown && !previousRightDown )
        {
            cancelPicker();
            return;
        }

        if( leftDown && !previousLeftDown
                && outside )
        {
            wxWindow* capture = wxWindow::GetCapture();
            bool dialogOwnsCapture = capture
                                     && ( capture == dialog || dialog->IsDescendant( capture ) );

            if( !dialogOwnsCapture )
                acceptOutsideColor();
        }

        previousLeftDown = leftDown;
        previousRightDown = rightDown;
    }

    bool sampleScreenColor( wxColour& aColor )
    {
        wxScreenDC screen;
        return screen.GetPixel( wxGetMousePosition(), &aColor ) && aColor.IsOk();
    }

    void setEyedropperCursor( bool aEnabled )
    {
        if( aEnabled )
        {
            // Re-apply while outside because the window under the pointer may replace the cursor
            // after an enter event.  This does not capture the mouse or consume its click.
            wxSetCursor( wxCursor( wxCURSOR_CROSS ) );
            eyedropperCursorSet = true;
        }
        else if( eyedropperCursorSet )
        {
            wxSetCursor( wxNullCursor );
            eyedropperCursorSet = false;
        }
    }

    void acceptOutsideColor()
    {
        if( finishing )
            return;

        finishing = true;
        eyedropperTimer.Stop();

        wxColour sampledColor;

        // wxScreenDC is backed by native screen capture on X11, Windows and macOS.  Restricted
        // compositors (notably Wayland without a capture portal) simply return false; in that
        // case the already selected gradient color is accepted unchanged.
        if( sampleScreenColor( sampledColor ) )
            setColor( COLOR4D( sampledColor ), true );

        dialog->EndDialogShim( wxID_OK );
    }

    void acceptSwatchColor( const COLOR4D& aColor )
    {
        if( finishing )
            return;

        setColor( aColor, true );
        finishing = true;
        eyedropperTimer.Stop();
        setEyedropperCursor( false );
        dialog->EndDialogShim( wxID_OK );
    }

    void cancelPicker()
    {
        if( finishing )
            return;

        finishing = true;
        eyedropperTimer.Stop();
        setEyedropperCursor( false );
        dialog->EndDialogShim( wxID_CANCEL );
    }

    DIALOG_COLOR_PICKER*          dialog;
    bool                          allowOpacity;
    COLOR4D                       color;
    COLOR4D                       defaultColor;
    COLOR4D                       displayColor;
    MODE                          mode;
    bool                          updatingControls;
    std::vector<NAMED_COLOR>      recent;
    COLOR_PICKER_CANVAS*          canvas;
    COLOR_SWATCH_GRID*            swatchGrid;
    wxButton*                     modeButton;
    wxTextCtrl*                   hexInput;
    std::array<wxStaticText*, 4>  channelLabels{};
    std::array<wxTextCtrl*, 4>    channelInputs{};
    std::array<int, 4>            channelMinimum{};
    std::array<int, 4>            channelMaximum{};
    wxButton*                     resetButton;
    wxButton*                     okButton;
    wxTimer                       eyedropperTimer;
    std::vector<wxWindow*>        rightClickWindows;
    bool                          previousLeftDown;
    bool                          previousRightDown;
    bool                          eyedropperCursorSet;
    SCREEN_PICKER_AVAILABILITY    screenPickerAvailability;
    bool                          finishing;
    bool                          interactionReady;
    bool                          shutdownComplete;
};


DIALOG_COLOR_PICKER::DIALOG_COLOR_PICKER( wxWindow* aParent, const COLOR4D& aCurrentColor,
                                          bool aAllowOpacityControl,
                                          std::vector<CUSTOM_COLOR_ITEM>* aUserColors,
                                          const COLOR4D& aDefaultColor ) :
        DIALOG_SHIM( aParent, wxID_ANY, _( "Color Picker" ), wxDefaultPosition, wxDefaultSize,
                     wxDEFAULT_DIALOG_STYLE ),
        m_impl( std::make_unique<IMPL>( this, aCurrentColor, aAllowOpacityControl, aUserColors,
                                       aDefaultColor ) )
{
    // This transient dialog should always use its calculated compact size, not a size persisted
    // by an older, resizable color picker implementation.
    m_useCalculatedSize = true;
    OptOut( this );
    finishDialogSettings();
}


DIALOG_COLOR_PICKER::~DIALOG_COLOR_PICKER() = default;


COLOR4D DIALOG_COLOR_PICKER::GetColor() const
{
    return m_impl->color;
}


bool DIALOG_COLOR_PICKER::Show( bool aShow )
{
    if( !aShow )
        m_impl->stopOutsideMonitor();

    bool shown = DIALOG_SHIM::Show( aShow );

    if( aShow )
    {
        m_impl->positionNearCursor();
        m_impl->startOutsideMonitor();
    }
    return shown;
}


void DIALOG_COLOR_PICKER::OnCharHook( wxKeyEvent& aEvent )
{
    if( aEvent.GetKeyCode() == WXK_ESCAPE )
    {
        EndDialogShim( wxID_CANCEL );
        return;
    }

    if( ( aEvent.GetKeyCode() == WXK_RETURN || aEvent.GetKeyCode() == WXK_NUMPAD_ENTER )
            && !aEvent.ControlDown() && !aEvent.ShiftDown() && !aEvent.AltDown() )
    {
        if( m_impl->commitFocusedEditor() )
        {
            m_impl->addRecent( m_impl->color, true );
            EndDialogShim( wxID_OK );
        }

        return;
    }

    DIALOG_SHIM::OnCharHook( aEvent );
}
