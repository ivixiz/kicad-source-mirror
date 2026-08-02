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

#include <sch_scope.h>

#include <base_units.h>
#include <bitmaps.h>
#include <font/font.h>
#include <font/text_attributes.h>
#include <geometry/geometry_utils.h>
#include <lib_symbol.h>
#include <plotters/plotter.h>
#include <sch_field.h>
#include <sch_pin.h>
#include <sch_symbol.h>
#include <sch_text.h>
#include <units_provider.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <nlohmann/json.hpp>
#include <unordered_map>
#include <utility>
#include <vector>


namespace
{
#if 0
// Deprecated channel-based scope prototype.  Kept as a reference for its pin generation,
// channel controls, and V/I/P cycling logic; it is intentionally excluded from compilation.
const wxString SCOPE_MODE_VOLTAGE = wxS( "V" );
const wxString SCOPE_MODE_CURRENT = wxS( "I" );
const wxString SCOPE_MODE_POWER   = wxS( "P" );
const wxString SCOPE_ADD_CHANNEL  = wxS( "+" );
const wxString SCOPE_REMOVE_CHANNEL = wxS( "-" );


bool isChannelModeText( const wxString& aText )
{
    return aText == SCOPE_MODE_VOLTAGE || aText == SCOPE_MODE_CURRENT
           || aText == SCOPE_MODE_POWER;
}


wxString nextChannelModeText( const wxString& aText )
{
    if( aText == SCOPE_MODE_VOLTAGE )
        return SCOPE_MODE_CURRENT;

    if( aText == SCOPE_MODE_CURRENT )
        return SCOPE_MODE_POWER;

    return SCOPE_MODE_VOLTAGE;
}


wxString channelPinNumber( int aChannel, bool aPositive )
{
    return wxString::Format( wxS( "%d" ), ( aChannel - 1 ) * 2 + ( aPositive ? 1 : 2 ) );
}


wxString channelPinName( int aChannel, bool aPositive )
{
    return wxString::Format( wxS( "CH%d%s" ), aChannel, aPositive ? wxS( "+" ) : wxS( "-" ) );
}


int channelStackHeight( int aChannelCount )
{
    return aChannelCount * SCH_SCOPE::PinPitch()
           + ( aChannelCount - 1 ) * SCH_SCOPE::PinPitch();
}


int channelMargin()
{
    return SCH_SCOPE::GridSize() * 2;
}


int channelTopY()
{
    return channelMargin();
}


bool getPinY( const LIB_SYMBOL* aSymbol, int aChannel, bool aPositive, int& aY )
{
    if( !aSymbol )
        return false;

    const SCH_PIN* pin = aSymbol->GetPin( channelPinNumber( aChannel, aPositive ) );

    if( !pin )
        return false;

    aY = pin->GetPosition().y;
    return true;
}


int fallbackPinY( const VECTOR2I& aBodySize, int aChannelCount, int aChannel, bool aPositive )
{
    (void) aBodySize;
    (void) aChannelCount;

    const int step = SCH_SCOPE::PinPitch() * 2;
    const int top = channelTopY();

    return top + ( aChannel - 1 ) * step + ( aPositive ? 0 : SCH_SCOPE::PinPitch() );
}


int channelCenterY( const LIB_SYMBOL* aSymbol, const VECTOR2I& aBodySize, int aChannel )
{
    int yPositive = 0;
    int yNegative = 0;

    if( getPinY( aSymbol, aChannel, true, yPositive )
        && getPinY( aSymbol, aChannel, false, yNegative ) )
    {
        return ( yPositive + yNegative ) / 2;
    }

    return aBodySize.y / 2;
}


BOX2I controlButtonBox( const LIB_SYMBOL* aSymbol, const VECTOR2I& aBodySize,
                        SCH_SCOPE::CONTROL aControl, int aChannel )
{
    const int size = SCH_SCOPE::ChannelModeButtonSize();
    VECTOR2I  center( SCH_SCOPE::GridSize(), channelCenterY( aSymbol, aBodySize, aChannel ) );

    if( aControl == SCH_SCOPE::CONTROL::ADD_CHANNEL
        || aControl == SCH_SCOPE::CONTROL::REMOVE_CHANNEL )
    {
        center.y = SCH_SCOPE::GridSize();

        if( aControl == SCH_SCOPE::CONTROL::REMOVE_CHANNEL )
            center.x += size + SCH_SCOPE::GridSize() / 2;
    }

    return BOX2I::ByCorners( center - VECTOR2I( size / 2, size / 2 ),
                             center + VECTOR2I( size / 2, size / 2 ) );
}


const SCH_SHAPE* findScopeBody( const LIB_SYMBOL* aSymbol )
{
    if( !aSymbol )
        return nullptr;

    const SCH_SHAPE* bestShape = nullptr;
    long long        bestArea = -1;

    for( const SCH_ITEM& item : aSymbol->GetDrawItems() )
    {
        if( item.Type() != SCH_SHAPE_T )
            continue;

        const SCH_SHAPE* shape = static_cast<const SCH_SHAPE*>( &item );

        if( shape->GetShape() != SHAPE_T::RECTANGLE )
            continue;

        const long long width = std::abs( shape->GetEnd().x - shape->GetStart().x );
        const long long height = std::abs( shape->GetEnd().y - shape->GetStart().y );
        const long long area = width * height;

        if( area > bestArea )
        {
            bestShape = shape;
            bestArea = area;
        }
    }

    return bestShape;
}


SCH_SHAPE* findScopeBody( LIB_SYMBOL* aSymbol )
{
    return const_cast<SCH_SHAPE*>( findScopeBody( static_cast<const LIB_SYMBOL*>( aSymbol ) ) );
}


VECTOR2I scopeBodySize( const LIB_SYMBOL* aSymbol )
{
    if( const SCH_SHAPE* body = findScopeBody( aSymbol ) )
    {
        return VECTOR2I( std::abs( body->GetEnd().x - body->GetStart().x ),
                         std::abs( body->GetEnd().y - body->GetStart().y ) );
    }

    return SCH_SCOPE::DefaultSize();
}


bool isControlButtonShape( const SCH_SHAPE* aShape )
{
    if( !aShape || aShape->GetShape() != SHAPE_T::RECTANGLE )
        return false;

    const int maxButtonSize = SCH_SCOPE::GridSize() * 2;
    const int width = std::abs( aShape->GetEnd().x - aShape->GetStart().x );
    const int height = std::abs( aShape->GetEnd().y - aShape->GetStart().y );

    return width <= maxButtonSize && height <= maxButtonSize;
}


BOX2I shapeBox( const SCH_SHAPE* aShape )
{
    BOX2I box = BOX2I::ByCorners( aShape->GetStart(), aShape->GetEnd() );
    box.Normalize();
    return box;
}


std::vector<SCH_TEXT*> sortedChannelModeTexts( LIB_SYMBOL* aSymbol )
{
    std::vector<SCH_TEXT*> texts;

    if( !aSymbol )
        return texts;

    for( SCH_ITEM& item : aSymbol->GetDrawItems() )
    {
        if( item.Type() != SCH_TEXT_T )
            continue;

        SCH_TEXT* text = static_cast<SCH_TEXT*>( &item );

        if( isChannelModeText( text->GetText() ) )
            texts.push_back( text );
    }

    std::sort( texts.begin(), texts.end(),
               []( const SCH_TEXT* aLeft, const SCH_TEXT* aRight )
               {
                   const VECTOR2I leftPos = aLeft->GetPosition();
                   const VECTOR2I rightPos = aRight->GetPosition();

                   if( leftPos.y != rightPos.y )
                       return leftPos.y < rightPos.y;

                   return leftPos.x < rightPos.x;
               } );

    return texts;
}


SCH_TEXT* findChannelModeText( LIB_SYMBOL* aSymbol, int aChannel )
{
    std::vector<SCH_TEXT*> texts = sortedChannelModeTexts( aSymbol );

    if( aChannel <= 0 || static_cast<size_t>( aChannel ) > texts.size() )
        return nullptr;

    return texts[static_cast<size_t>( aChannel - 1 )];
}


SCH_TEXT* findControlText( LIB_SYMBOL* aSymbol, SCH_SCOPE::CONTROL aControl, int aChannel )
{
    if( !aSymbol )
        return nullptr;

    if( aControl == SCH_SCOPE::CONTROL::CHANNEL_MODE )
        return findChannelModeText( aSymbol, aChannel );

    for( SCH_ITEM& item : aSymbol->GetDrawItems() )
    {
        if( item.Type() != SCH_TEXT_T )
            continue;

        SCH_TEXT* text = static_cast<SCH_TEXT*>( &item );

        if( aControl == SCH_SCOPE::CONTROL::ADD_CHANNEL && text->GetText() == SCOPE_ADD_CHANNEL )
            return text;

        if( aControl == SCH_SCOPE::CONTROL::REMOVE_CHANNEL
            && text->GetText() == SCOPE_REMOVE_CHANNEL )
        {
            return text;
        }
    }

    return nullptr;
}


SCH_SHAPE* findControlButton( LIB_SYMBOL* aSymbol, SCH_SCOPE::CONTROL aControl,
                              const BOX2I& aButtonBox, int aChannel )
{
    if( !aSymbol )
        return nullptr;

    SCH_TEXT* controlText = findControlText( aSymbol, aControl, aChannel );

    if( controlText )
    {
        const VECTOR2I textPos = controlText->GetPosition();

        for( SCH_ITEM& item : aSymbol->GetDrawItems() )
        {
            if( item.Type() != SCH_SHAPE_T )
                continue;

            SCH_SHAPE* shape = static_cast<SCH_SHAPE*>( &item );

            if( isControlButtonShape( shape ) && shapeBox( shape ).Contains( textPos ) )
                return shape;
        }
    }

    const VECTOR2I targetCenter = aButtonBox.GetCenter();

    for( SCH_ITEM& item : aSymbol->GetDrawItems() )
    {
        if( item.Type() != SCH_SHAPE_T )
            continue;

        SCH_SHAPE* shape = static_cast<SCH_SHAPE*>( &item );

        if( !isControlButtonShape( shape ) )
            continue;

        const VECTOR2I center = shapeBox( shape ).GetCenter();

        if( std::abs( center.x - targetCenter.x ) <= SCH_SCOPE::GridSize() / 2
            && std::abs( center.y - targetCenter.y ) <= SCH_SCOPE::GridSize() / 2 )
        {
            return shape;
        }
    }

    return nullptr;
}


void removeControlForText( LIB_SYMBOL* aSymbol, SCH_TEXT* aText )
{
    if( !aSymbol || !aText )
        return;

    SCH_SHAPE* button = nullptr;
    const VECTOR2I textPos = aText->GetPosition();

    for( SCH_ITEM& item : aSymbol->GetDrawItems() )
    {
        if( item.Type() != SCH_SHAPE_T )
            continue;

        SCH_SHAPE* shape = static_cast<SCH_SHAPE*>( &item );

        if( isControlButtonShape( shape ) && shapeBox( shape ).Contains( textPos ) )
        {
            button = shape;
            break;
        }
    }

    if( button )
        aSymbol->RemoveDrawItem( button );

    aSymbol->RemoveDrawItem( aText );
}


void removeExtraChannelModeControls( LIB_SYMBOL* aSymbol, int aChannelCount )
{
    std::vector<SCH_TEXT*> texts = sortedChannelModeTexts( aSymbol );

    for( size_t ii = static_cast<size_t>( std::max( 0, aChannelCount ) ); ii < texts.size(); ++ii )
        removeControlForText( aSymbol, texts[ii] );
}


void configureControl( LIB_SYMBOL* aSymbol, const VECTOR2I& aBodySize, SCH_SCOPE::CONTROL aControl,
                       int aChannel = 0 )
{
    const BOX2I buttonBox = controlButtonBox( aSymbol, aBodySize, aControl, aChannel );

    SCH_SHAPE* button = findControlButton( aSymbol, aControl, buttonBox, aChannel );

    if( !button )
    {
        button = new SCH_SHAPE( SHAPE_T::RECTANGLE, LAYER_DEVICE, 0,
                                FILL_T::FILLED_WITH_BG_BODYCOLOR );
    }

    button->SetStart( buttonBox.GetPosition() );
    button->SetEnd( buttonBox.GetEnd() );
    button->SetLayer( LAYER_DEVICE );
    button->SetFillMode( FILL_T::FILLED_WITH_BG_BODYCOLOR );

    if( !button->GetParent() )
        aSymbol->AddDrawItem( button );

    SCH_TEXT* controlText = findControlText( aSymbol, aControl, aChannel );
    wxString  label = SCOPE_MODE_VOLTAGE;

    if( aControl == SCH_SCOPE::CONTROL::ADD_CHANNEL )
        label = SCOPE_ADD_CHANNEL;
    else if( aControl == SCH_SCOPE::CONTROL::REMOVE_CHANNEL )
        label = SCOPE_REMOVE_CHANNEL;
    else if( controlText && isChannelModeText( controlText->GetText() ) )
        label = controlText->GetText();

    if( !controlText )
    {
        controlText = new SCH_TEXT( buttonBox.GetCenter(), label, LAYER_DEVICE );
        controlText->SetHorizJustify( GR_TEXT_H_ALIGN_CENTER );
        controlText->SetVertJustify( GR_TEXT_V_ALIGN_CENTER );
        controlText->SetMultilineAllowed( false );
    }

    controlText->SetText( label );
    controlText->SetPosition( buttonBox.GetCenter() );
    controlText->SetLayer( LAYER_DEVICE );
    controlText->SetTextSize( VECTOR2I( SCH_SCOPE::ChannelModeTextSize(),
                                         SCH_SCOPE::ChannelModeTextSize() ) );
    controlText->SetHorizJustify( GR_TEXT_H_ALIGN_CENTER );
    controlText->SetVertJustify( GR_TEXT_V_ALIGN_CENTER );
    controlText->SetMultilineAllowed( false );

    if( !controlText->GetParent() )
        aSymbol->AddDrawItem( controlText );
}


void setPinGeometry( LIB_SYMBOL* aSymbol, int aChannel, bool aPositive, int aY )
{
    const wxString number = channelPinNumber( aChannel, aPositive );
    const wxString name = channelPinName( aChannel, aPositive );
    const int      pinLength = SCH_SCOPE::PinLength();
    const int      pinTextSize = SCH_SCOPE::PinTextSize();

    std::vector<SCH_PIN*> pins = aSymbol->GetPinsByNumber( number );

    if( pins.empty() )
    {
        pins.push_back( new SCH_PIN( aSymbol, name, number, PIN_ORIENTATION::PIN_RIGHT,
                                     ELECTRICAL_PINTYPE::PT_INPUT, pinLength, pinTextSize,
                                     pinTextSize, 0, VECTOR2I( -pinLength, aY ), 0 ) );
        aSymbol->AddDrawItem( pins.back() );
    }

    for( SCH_PIN* pin : pins )
    {
        pin->SetName( name );
        pin->SetOrientation( PIN_ORIENTATION::PIN_RIGHT );
        pin->SetType( ELECTRICAL_PINTYPE::PT_INPUT );
        pin->SetLength( pinLength );
        pin->SetNameTextSize( pinTextSize );
        pin->SetNumberTextSize( pinTextSize );
        pin->SetPosition( VECTOR2I( -pinLength, aY ) );
        pin->SetVisible( true );
    }
}


void removePinsByNumber( LIB_SYMBOL* aSymbol, const wxString& aNumber )
{
    std::vector<SCH_PIN*> pins = aSymbol->GetPinsByNumber( aNumber );

    for( SCH_PIN* pin : pins )
        aSymbol->RemoveDrawItem( pin );
}
#endif

const wxString SCOPE_WAVEFORMS_FIELD = wxS( "__ScopeWaveforms" );
constexpr int  SCOPE_SETTINGS_VERSION = 3;
constexpr double SCOPE_ZERO_CLIP = 1e-18;
constexpr double SCOPE_VERTICAL_PADDING = 0.05;


double clipScopeNearZero( double aValue )
{
    return std::abs( aValue ) < SCOPE_ZERO_CLIP ? 0.0 : aValue;
}


wxString compactScopeNetName( wxString aNetName )
{
    const size_t netPrefix = aNetName.find( wxS( "Net-" ) );
    bool generatedName = false;

    if( netPrefix != wxString::npos )
    {
        wxString generated = aNetName.Mid( netPrefix + 4 );

        if( generated.length() > 2
            && ( ( generated.StartsWith( wxS( "*" ) ) && generated.EndsWith( wxS( "*" ) ) )
                 || ( generated.StartsWith( wxS( "(" ) ) && generated.EndsWith( wxS( ")" ) ) )
                 || ( generated.StartsWith( wxS( "_" ) ) && generated.EndsWith( wxS( "_" ) ) ) ) )
        {
            aNetName = generated.Mid( 1, generated.length() - 2 );
            generatedName = true;
        }
    }

    if( generatedName )
        aNetName.Replace( wxS( "-" ), wxEmptyString );

    return aNetName;
}

struct SCOPE_RUNTIME_STATE
{
    std::vector<SCH_SCOPE::WAVEFORM> waveforms;
    std::vector<SCH_SCOPE::CURSOR>   cursors;
    SCH_SCOPE::CURSOR_MEASUREMENT    cursorMeasurement;
    SCH_SCOPE::VIEWPORT              viewport;
    SCH_SCOPE::DATA_BOUNDS           bounds;
    SCH_SCOPE::AXIS_INFO             axisInfo;
    SCH_SCOPE::ZOOM_SELECTION        zoomSelection;
    wxString                         cursorSourceName;
};


struct SCOPE_SETTINGS_CACHE
{
    wxString            serialized;
    SCH_SCOPE::SETTINGS settings;
};


std::unordered_map<KIID, SCOPE_RUNTIME_STATE> s_scopeRuntime;
std::unordered_map<KIID, SCOPE_SETTINGS_CACHE> s_scopeSettingsCache;


KIGFX::COLOR4D colorFromHex( const char* aColor )
{
    return KIGFX::COLOR4D( wxString::FromUTF8( aColor ) );
}


KIGFX::COLOR4D parseColor( const nlohmann::json& aJson, const char* aKey,
                           const KIGFX::COLOR4D& aDefault )
{
    if( !aJson.contains( aKey ) || !aJson[aKey].is_string() )
        return aDefault;

    KIGFX::COLOR4D color;
    wxString       value = wxString::FromUTF8( aJson[aKey].get<std::string>() );

    return color.SetFromWxString( value ) ? color : aDefault;
}


int sanitizeWidth( int aWidth, int aDefault )
{
    return std::clamp( aWidth > 0 ? aWidth : aDefault, 1, schIUScale.mmToIU( 10.0 ) );
}


SCH_SCOPE::SETTINGS sanitizeSettings( SCH_SCOPE::SETTINGS aSettings )
{
    SCH_SCOPE::SETTINGS defaults = SCH_SCOPE::DefaultSettings();
    std::vector<SCH_SCOPE::WAVEFORM_SOURCE> sources;

    for( SCH_SCOPE::WAVEFORM_SOURCE& source : aSettings.sources )
    {
        if( source.name.IsEmpty() )
            continue;

        bool duplicate = std::any_of( sources.begin(), sources.end(),
                                      [&]( const SCH_SCOPE::WAVEFORM_SOURCE& aExisting )
                                      {
                                          return aExisting.name == source.name;
                                      } );

        if( duplicate )
            continue;

        source.lineWidth = sanitizeWidth( source.lineWidth,
                                          defaults.sources.empty()
                                                  ? schIUScale.mmToIU( 0.2 )
                                                  : defaults.sources.front().lineWidth );
        sources.push_back( source );
    }

    aSettings.sources = std::move( sources );
    aSettings.borderWidth = sanitizeWidth( aSettings.borderWidth, defaults.borderWidth );
    aSettings.gridWidth = sanitizeWidth( aSettings.gridWidth, defaults.gridWidth );
    aSettings.minorGridWidth = sanitizeWidth( aSettings.minorGridWidth,
                                              defaults.minorGridWidth );
    aSettings.axisTextSize = sanitizeWidth( aSettings.axisTextSize, defaults.axisTextSize );

    if( aSettings.gridStyle < LINE_STYLE::FIRST_TYPE
        || aSettings.gridStyle > LINE_STYLE::LAST_TYPE )
    {
        aSettings.gridStyle = LINE_STYLE::SOLID;
    }

    if( aSettings.minorGridStyle < LINE_STYLE::FIRST_TYPE
        || aSettings.minorGridStyle > LINE_STYLE::LAST_TYPE )
    {
        aSettings.minorGridStyle = LINE_STYLE::SOLID;
    }

    return aSettings;
}


SCH_SCOPE::SETTINGS parseSettings( const wxString& aSerialized )
{
    SCH_SCOPE::SETTINGS settings = SCH_SCOPE::DefaultSettings();

    if( aSerialized.IsEmpty() )
        return settings;

    nlohmann::json root = nlohmann::json::parse( std::string( aSerialized.ToUTF8() ), nullptr,
                                                 false );

    if( root.is_discarded() || !root.is_object() )
    {
        wxArrayString values = wxSplit( aSerialized, '\n', '\0' );

        for( const wxString& value : values )
        {
            if( !value.IsEmpty() )
            {
                const size_t index = settings.sources.size();
                settings.sources.push_back( { value, SCH_SCOPE::DefaultWaveformColor( index ),
                                               schIUScale.mmToIU( 0.2 ) } );
            }
        }

        return settings;
    }

    settings.backgroundColor = parseColor( root, "background", settings.backgroundColor );

    if( root.contains( "border" ) && root["border"].is_object() )
    {
        const nlohmann::json& border = root["border"];
        settings.borderColor = parseColor( border, "color", settings.borderColor );
        settings.borderWidth = border.value( "width", settings.borderWidth );
    }

    if( root.contains( "grid" ) && root["grid"].is_object() )
    {
        const nlohmann::json& grid = root["grid"];
        settings.gridVisible = grid.value( "visible", settings.gridVisible );
        settings.gridColor = parseColor( grid, "color", settings.gridColor );
        settings.gridWidth = grid.value( "width", settings.gridWidth );
        settings.gridStyle = static_cast<LINE_STYLE>(
                grid.value( "style", static_cast<int>( settings.gridStyle ) ) );
    }

    if( root.contains( "minor_grid" ) && root["minor_grid"].is_object() )
    {
        const nlohmann::json& grid = root["minor_grid"];
        settings.minorGridVisible = grid.value( "visible", settings.minorGridVisible );
        settings.minorGridColor = parseColor( grid, "color", settings.minorGridColor );
        settings.minorGridWidth = grid.value( "width", settings.minorGridWidth );
        settings.minorGridStyle = static_cast<LINE_STYLE>(
                grid.value( "style", static_cast<int>( settings.minorGridStyle ) ) );
    }

    if( root.contains( "axis" ) && root["axis"].is_object() )
    {
        const nlohmann::json& axis = root["axis"];

        if( axis.contains( "font" ) && axis["font"].is_string() )
            settings.axisFontName = wxString::FromUTF8( axis["font"].get<std::string>() );

        settings.axisTextSize = axis.value( "text_size", settings.axisTextSize );
    }

    settings.sources.clear();

    if( root.contains( "sources" ) && root["sources"].is_array() )
    {
        for( const nlohmann::json& entry : root["sources"] )
        {
            if( !entry.is_object() || !entry.contains( "name" ) || !entry["name"].is_string() )
                continue;

            SCH_SCOPE::WAVEFORM_SOURCE source;
            source.name = wxString::FromUTF8( entry["name"].get<std::string>() );
            source.color = parseColor( entry, "color",
                                       SCH_SCOPE::DefaultWaveformColor( settings.sources.size() ) );
            source.lineWidth = entry.value( "width", schIUScale.mmToIU( 0.2 ) );
            settings.sources.push_back( source );
        }
    }

    return sanitizeSettings( settings );
}


wxString serializeSettings( const SCH_SCOPE::SETTINGS& aSettings )
{
    nlohmann::json root;
    root["version"] = SCOPE_SETTINGS_VERSION;
    root["background"] = std::string( aSettings.backgroundColor.ToCSSString().ToUTF8() );
    root["border"] = {
        { "color", std::string( aSettings.borderColor.ToCSSString().ToUTF8() ) },
        { "width", aSettings.borderWidth }
    };
    root["grid"] = {
        { "visible", aSettings.gridVisible },
        { "style", static_cast<int>( aSettings.gridStyle ) },
        { "color", std::string( aSettings.gridColor.ToCSSString().ToUTF8() ) },
        { "width", aSettings.gridWidth }
    };
    root["minor_grid"] = {
        { "visible", aSettings.minorGridVisible },
        { "style", static_cast<int>( aSettings.minorGridStyle ) },
        { "color", std::string( aSettings.minorGridColor.ToCSSString().ToUTF8() ) },
        { "width", aSettings.minorGridWidth }
    };
    root["axis"] = {
        { "font", std::string( aSettings.axisFontName.ToUTF8() ) },
        { "text_size", aSettings.axisTextSize }
    };
    root["sources"] = nlohmann::json::array();

    for( const SCH_SCOPE::WAVEFORM_SOURCE& source : aSettings.sources )
    {
        root["sources"].push_back( {
            { "name", std::string( source.name.ToUTF8() ) },
            { "color", std::string( source.color.ToCSSString().ToUTF8() ) },
            { "width", source.lineWidth }
        } );
    }

    return wxString::FromUTF8( root.dump() );
}


bool sameSources( const SCH_SCOPE::SETTINGS& aLeft, const SCH_SCOPE::SETTINGS& aRight )
{
    if( aLeft.sources.size() != aRight.sources.size() )
        return false;

    for( size_t ii = 0; ii < aLeft.sources.size(); ++ii )
    {
        if( aLeft.sources[ii].name != aRight.sources[ii].name )
            return false;
    }

    return true;
}


bool sampleWaveformAtX( const SCH_SCOPE::WAVEFORM& aWaveform, double aX, double& aY )
{
    const size_t size = std::min( aWaveform.x.size(), aWaveform.y.size() );

    if( size < 2 || aX < aWaveform.minX || aX > aWaveform.maxX )
        return false;

    auto interpolate = [&]( size_t aFirst, size_t aSecond )
    {
        const double x1 = aWaveform.x[aFirst];
        const double x2 = aWaveform.x[aSecond];
        const double y1 = aWaveform.y[aFirst];
        const double y2 = aWaveform.y[aSecond];

        if( !std::isfinite( x1 ) || !std::isfinite( x2 )
            || !std::isfinite( y1 ) || !std::isfinite( y2 ) )
        {
            return false;
        }

        if( x1 == x2 )
        {
            aY = y2;
            return true;
        }

        const double ratio = std::clamp( ( aX - x1 ) / ( x2 - x1 ), 0.0, 1.0 );
        aY = y1 + ratio * ( y2 - y1 );
        return std::isfinite( aY );
    };

    if( aWaveform.monotonicX )
    {
        auto begin = aWaveform.x.begin();
        auto end = begin + static_cast<ptrdiff_t>( size );
        auto upper = std::lower_bound( begin, end, aX );

        if( upper == begin )
            return interpolate( 0, 1 );

        if( upper == end )
            return interpolate( size - 2, size - 1 );

        const size_t second = static_cast<size_t>( upper - begin );
        return interpolate( second - 1, second );
    }

    for( size_t ii = 1; ii < size; ++ii )
    {
        const double x1 = aWaveform.x[ii - 1];
        const double x2 = aWaveform.x[ii];

        if( std::isfinite( x1 ) && std::isfinite( x2 )
            && aX >= std::min( x1, x2 ) && aX <= std::max( x1, x2 )
            && interpolate( ii - 1, ii ) )
        {
            return true;
        }
    }

    return false;
}


void updateCursorIntersections( const SCH_SCOPE::SETTINGS& aSettings,
                                SCOPE_RUNTIME_STATE& aRuntime )
{
    aRuntime.cursorSourceName = aSettings.sources.empty() ? wxString()
                                                           : aSettings.sources.front().name;

    for( SCH_SCOPE::CURSOR& cursor : aRuntime.cursors )
        cursor.valid = false;

    if( aRuntime.cursorSourceName.IsEmpty() )
        return;

    const auto waveform = std::find_if(
            aRuntime.waveforms.begin(), aRuntime.waveforms.end(),
            [&]( const SCH_SCOPE::WAVEFORM& aWaveform )
            {
                return aWaveform.name == aRuntime.cursorSourceName;
            } );

    const double xRange = aRuntime.bounds.maxX - aRuntime.bounds.minX;
    const double yRange = aRuntime.bounds.maxY - aRuntime.bounds.minY;

    if( waveform == aRuntime.waveforms.end() || xRange <= 0.0 || yRange <= 0.0 )
        return;

    for( SCH_SCOPE::CURSOR& cursor : aRuntime.cursors )
    {
        const double x = aRuntime.bounds.minX + cursor.x * xRange;
        double       y = 0.0;

        if( sampleWaveformAtX( *waveform, x, y ) )
        {
            cursor.y = ( y - aRuntime.bounds.minY ) / yRange;
            cursor.valid = std::isfinite( cursor.y );
        }
    }
}


void updateCursorReadout( const SCH_SYMBOL* aSymbol, const SCH_SCOPE::SETTINGS& aSettings,
                          SCOPE_RUNTIME_STATE& aRuntime )
{
    SCH_SCOPE::CURSOR_MEASUREMENT& measurement = aRuntime.cursorMeasurement;
    const bool hadPosition = measurement.valid;
    const double previousArrowY = measurement.arrowY;
    const double previousYDeltaX = measurement.yDeltaX;
    measurement = SCH_SCOPE::CURSOR_MEASUREMENT();

    if( !aSymbol || aRuntime.cursors.empty() || aSettings.sources.empty() )
    {
        return;
    }

    const double xRange = aRuntime.bounds.maxX - aRuntime.bounds.minX;
    const double yRange = aRuntime.bounds.maxY - aRuntime.bounds.minY;

    if( xRange <= 0.0 || yRange <= 0.0 )
        return;

    if( aRuntime.cursors.size() == 1 )
    {
        const SCH_SCOPE::CURSOR& cursor = aRuntime.cursors.front();

        if( !cursor.valid )
            return;
        measurement.cursorXLabel = //wxS( "X=" ) + 
                                   SCH_SCOPE::FormatEngineeringValue(
                                           aRuntime.bounds.minX + cursor.x * xRange );
        measurement.cursorYLabel = //wxS( "Y=" ) + 
                                   SCH_SCOPE::FormatEngineeringValue(
                                           aRuntime.bounds.minY + cursor.y * yRange );
        measurement.singleCursorValid = true;
        measurement.valid = true;
        return;
    }

    if( aRuntime.cursors.size() != 2 || !aRuntime.cursors[0].valid
        || !aRuntime.cursors[1].valid )
    {
        return;
    }

    const double cursorMinX = std::min( aRuntime.cursors[0].x, aRuntime.cursors[1].x );
    const double cursorMaxX = std::max( aRuntime.cursors[0].x, aRuntime.cursors[1].x );
    const double period = ( cursorMaxX - cursorMinX ) * xRange;
    const SCH_SCOPE::LAYOUT layout = SCH_SCOPE::GetLayout( aSymbol );

    if( !std::isfinite( period ) || period <= std::numeric_limits<double>::epsilon()
        || xRange <= 0.0 || layout.plotBox.GetWidth() <= 0
        || layout.plotBox.GetHeight() <= 0 )
    {
        return;
    }

    measurement.frequencyLabel = wxS( "f=" )
                                 + SCH_SCOPE::FormatEngineeringValue( 1.0 / period )
                                 + wxS( "Hz" );
    measurement.periodLabel = wxS( "T=" ) + SCH_SCOPE::FormatEngineeringValue( period )
                              + wxS( "s" );
    measurement.yDeltaLabel = //wxS( "dY=" ) + 
                              SCH_SCOPE::FormatEngineeringValue(
                                      std::abs( aRuntime.cursors[1].y
                                                - aRuntime.cursors[0].y )
                                      * yRange );

    const int textSize = std::max( 1, layout.textSize * 4 / 5 );
    const int lineWidth = std::max( 1, aSettings.minorGridWidth );
    const int textGap = std::max( textSize * 2 / 3, lineWidth * 4 );
    const int inset = std::min( static_cast<int>( layout.plotBox.GetHeight() ) / 2,
                                textSize + textGap / 2 );
    constexpr double edgeClearance = 0.05;
    measurement.arrowY = hadPosition
                                 ? previousArrowY
                                 : std::min( 0.5, edgeClearance
                                                         + static_cast<double>( inset )
                                                                   / layout.plotBox.GetHeight() );
    measurement.yDeltaX = hadPosition ? previousYDeltaX : 0.95;
    measurement.valid = true;
}


void updateCursorMeasurement( const SCH_SYMBOL* aSymbol, const SCH_SCOPE::SETTINGS& aSettings,
                              SCOPE_RUNTIME_STATE& aRuntime )
{
    updateCursorReadout( aSymbol, aSettings, aRuntime );

    SCH_SCOPE::CURSOR_MEASUREMENT& measurement = aRuntime.cursorMeasurement;

    if( !measurement.valid || aRuntime.cursors.size() != 2 )
        return;

    const double xRange = aRuntime.bounds.maxX - aRuntime.bounds.minX;
    const double yRange = aRuntime.bounds.maxY - aRuntime.bounds.minY;
    const double viewportXRange = aRuntime.viewport.xMax - aRuntime.viewport.xMin;
    const double viewportYRange = aRuntime.viewport.yMax - aRuntime.viewport.yMin;
    const double cursorMinX = std::min( aRuntime.cursors[0].x, aRuntime.cursors[1].x );
    const double cursorMaxX = std::max( aRuntime.cursors[0].x, aRuntime.cursors[1].x );
    const SCH_SCOPE::LAYOUT layout = SCH_SCOPE::GetLayout( aSymbol );
    const int textSize = std::max( 1, layout.textSize * 4 / 5 );
    const int lineWidth = std::max( 1, aSettings.minorGridWidth );
    const int textGap = std::max( textSize * 2 / 3, lineWidth * 4 );
    const int inset = std::min( static_cast<int>( layout.plotBox.GetHeight() ) / 2,
                                textSize + textGap / 2 );
    constexpr double edgeClearance = 0.05;
    const double topCandidate = std::min(
            0.5, edgeClearance + static_cast<double>( inset ) / layout.plotBox.GetHeight() );
    const double bottomCandidate = 1.0 - topCandidate;
    const double occupiedHeight = std::min(
            0.5, edgeClearance + static_cast<double>( textSize * 2 + textGap )
                                         / layout.plotBox.GetHeight() );
    double topClearance = 1.0;
    double bottomClearance = 1.0;

    const auto waveform = std::find_if(
            aRuntime.waveforms.begin(), aRuntime.waveforms.end(),
            [&]( const SCH_SCOPE::WAVEFORM& aWaveform )
            {
                return aWaveform.name == aSettings.sources.front().name;
            } );

    auto considerSample = [&]( double aX, double aY )
    {
        if( !std::isfinite( aX ) || !std::isfinite( aY ) )
            return;

        const double normalizedX = ( aX - aRuntime.bounds.minX ) / xRange;
        const double normalizedY = ( aY - aRuntime.bounds.minY ) / yRange;

        if( normalizedX < cursorMinX || normalizedX > cursorMaxX
            || normalizedX < aRuntime.viewport.xMin || normalizedX > aRuntime.viewport.xMax
            || normalizedY < aRuntime.viewport.yMin || normalizedY > aRuntime.viewport.yMax )
        {
            return;
        }

        const double screenY = 1.0 - ( normalizedY - aRuntime.viewport.yMin )
                                             / viewportYRange;
        const double topDistance = std::max( 0.0, screenY - occupiedHeight );
        const double bottomDistance = std::max( 0.0, 1.0 - occupiedHeight - screenY );
        topClearance = std::min( topClearance, topDistance );
        bottomClearance = std::min( bottomClearance, bottomDistance );
    };

    if( waveform != aRuntime.waveforms.end() )
    {
        const size_t size = std::min( waveform->x.size(), waveform->y.size() );

        if( waveform->monotonicX && size >= 2 )
        {
            const double visibleMin = std::max( cursorMinX, aRuntime.viewport.xMin );
            const double visibleMax = std::min( cursorMaxX, aRuntime.viewport.xMax );

            if( visibleMin <= visibleMax )
            {
                constexpr int sampleCount = 64;

                for( int ii = 0; ii <= sampleCount; ++ii )
                {
                    const double normalizedX = visibleMin
                                               + ( visibleMax - visibleMin ) * ii / sampleCount;
                    const double x = aRuntime.bounds.minX + normalizedX * xRange;
                    double       y = 0.0;

                    if( sampleWaveformAtX( *waveform, x, y ) )
                        considerSample( x, y );
                }
            }
        }
        else if( size > 0 )
        {
            const size_t stride = std::max<size_t>( 1, size / 256 );

            for( size_t ii = 0; ii < size; ii += stride )
                considerSample( waveform->x[ii], waveform->y[ii] );

            considerSample( waveform->x[size - 1], waveform->y[size - 1] );
        }
    }

    measurement.arrowY = topClearance >= bottomClearance ? topCandidate : bottomCandidate;
    measurement.arrowVisible = true;
    const int horizontalInset = std::min( static_cast<int>( layout.plotBox.GetWidth() ) / 2,
                                          textSize + textGap / 2 );
    const double sideInset = std::min(
            0.5, edgeClearance + static_cast<double>( horizontalInset )
                                         / layout.plotBox.GetWidth() );
    const double cursorCenterX =
            viewportXRange > 0.0
                    ? ( ( cursorMinX + cursorMaxX ) / 2.0 - aRuntime.viewport.xMin )
                              / viewportXRange
                    : 0.5;
    measurement.yDeltaX = cursorCenterX < 0.5 ? 1.0 - sideInset : sideInset;
    measurement.yDeltaVisible = true;
    measurement.valid = true;
}


void clampViewportAxis( double& aMin, double& aMax )
{
    constexpr double minRange = 1e-5;
    double           range = std::clamp( aMax - aMin, minRange, 1.0 );

    if( aMin < 0.0 )
    {
        aMin = 0.0;
        aMax = range;
    }
    else if( aMax > 1.0 )
    {
        aMax = 1.0;
        aMin = 1.0 - range;
    }

    aMin = std::clamp( aMin, 0.0, 1.0 - minRange );
    aMax = std::clamp( aMax, aMin + minRange, 1.0 );
}


SCH_SHAPE* findScopeBody( LIB_SYMBOL* aSymbol )
{
    if( !aSymbol )
        return nullptr;

    SCH_SHAPE* bestShape = nullptr;
    long long  bestArea = -1;

    for( SCH_ITEM& item : aSymbol->GetDrawItems() )
    {
        if( item.Type() != SCH_SHAPE_T )
            continue;

        SCH_SHAPE* shape = static_cast<SCH_SHAPE*>( &item );

        if( shape->GetShape() != SHAPE_T::RECTANGLE )
            continue;

        const long long width = std::abs( shape->GetEnd().x - shape->GetStart().x );
        const long long height = std::abs( shape->GetEnd().y - shape->GetStart().y );
        const long long area = width * height;

        if( area > bestArea )
        {
            bestShape = shape;
            bestArea = area;
        }
    }

    return bestShape;
}
} // namespace


SCH_SCOPE::SCH_SCOPE( const VECTOR2I& aPosition, SCH_LAYER_ID aLayer, int aLineWidth,
                      FILL_T aFillType ) :
        SCH_SHAPE( SHAPE_T::RECTANGLE, aLayer, aLineWidth, aFillType, SCH_SHAPE_T )
{
    SetStart( aPosition );
    SetEnd( aPosition + DefaultSize() );

    const SETTINGS settings = DefaultSettings();
    SetFillMode( FILL_T::FILLED_WITH_COLOR );
    SetFillColor( settings.backgroundColor );
    SetStroke( STROKE_PARAMS( settings.borderWidth, LINE_STYLE::SOLID, settings.borderColor ) );
}


bool SCH_SCOPE::WAVEFORM_SOURCE::operator==( const WAVEFORM_SOURCE& aOther ) const
{
    return name == aOther.name && color == aOther.color && lineWidth == aOther.lineWidth;
}


bool SCH_SCOPE::SETTINGS::operator==( const SETTINGS& aOther ) const
{
    return sources == aOther.sources && backgroundColor == aOther.backgroundColor
           && borderColor == aOther.borderColor && borderWidth == aOther.borderWidth
           && gridVisible == aOther.gridVisible && gridStyle == aOther.gridStyle
           && gridColor == aOther.gridColor && gridWidth == aOther.gridWidth
           && minorGridVisible == aOther.minorGridVisible
           && minorGridStyle == aOther.minorGridStyle
           && minorGridColor == aOther.minorGridColor
           && minorGridWidth == aOther.minorGridWidth
           && axisFontName == aOther.axisFontName && axisTextSize == aOther.axisTextSize;
}


VECTOR2I SCH_SCOPE::DefaultSize()
{
    return VECTOR2I( schIUScale.MilsToIU( 800 ), schIUScale.MilsToIU( 500 ) );
}


VECTOR2I SCH_SCOPE::MinimumSize()
{
    return VECTOR2I( schIUScale.MilsToIU( 400 ), schIUScale.MilsToIU( 250 ) );
}


#if 0
VECTOR2I SCH_SCOPE::MinimumSize( int aChannelCount )
{
    const int channelCount = std::clamp( aChannelCount, 1, MaxChannelCount() );
    const int height = channelMargin() + channelStackHeight( channelCount ) + channelMargin();
    const int width = PinPitch() + GridSize();

    return VECTOR2I( width, height );
}
#endif


LIB_ID SCH_SCOPE::LibId()
{
    return LIB_ID( wxS( "Simulation" ), wxS( "Scope" ) );
}


bool SCH_SCOPE::IsScopeSymbol( const SCH_SYMBOL* aSymbol )
{
    return aSymbol && aSymbol->GetLibId() == LibId();
}


#if 0
int SCH_SCOPE::ChannelCount( const LIB_SYMBOL* aSymbol )
{
    if( !aSymbol )
        return 1;

    int count = 0;

    for( int channel = 1; channel <= MaxChannelCount(); ++channel )
    {
        if( aSymbol->GetPin( channelPinNumber( channel, true ) )
            && aSymbol->GetPin( channelPinNumber( channel, false ) ) )
        {
            count = channel;
        }
        else
        {
            break;
        }
    }

    return std::max( 1, count );
}


void SCH_SCOPE::UpdateChannelModeControl( LIB_SYMBOL* aSymbol, const VECTOR2I& aBodySize )
{
    if( !aSymbol )
        return;

    const int channelCount = ChannelCount( aSymbol );

    for( int channel = 1; channel <= channelCount; ++channel )
        configureControl( aSymbol, aBodySize, CONTROL::CHANNEL_MODE, channel );

    configureControl( aSymbol, aBodySize, CONTROL::ADD_CHANNEL );
    configureControl( aSymbol, aBodySize, CONTROL::REMOVE_CHANNEL );
    removeExtraChannelModeControls( aSymbol, channelCount );
}


void SCH_SCOPE::UpdateChannelGeometry( LIB_SYMBOL* aSymbol, const VECTOR2I& aBodySize )
{
    if( !aSymbol )
        return;

    const int channelCount = ChannelCount( aSymbol );
    const int step = PinPitch() * 2;
    const int top = channelTopY();

    aSymbol->SetShowPinNames( true );
    aSymbol->SetShowPinNumbers( false );
    aSymbol->SetPinNameOffset( PinNameOffset() );

    for( int channel = 1; channel <= channelCount; ++channel )
    {
        const int yPositive = top + ( channel - 1 ) * step;

        setPinGeometry( aSymbol, channel, true, yPositive );
        setPinGeometry( aSymbol, channel, false, yPositive + PinPitch() );
    }

    UpdateChannelModeControl( aSymbol, aBodySize );
}


SCH_SCOPE::CONTROL_HIT SCH_SCOPE::HitTestControl( const SCH_SYMBOL* aSymbol, const VECTOR2I& aPosition )
{
    if( !IsScopeSymbol( aSymbol ) || !aSymbol->GetLibSymbolRef() )
        return {};

    const LIB_SYMBOL* libSymbol = aSymbol->GetLibSymbolRef().get();
    const VECTOR2I    bodySize = scopeBodySize( libSymbol );
    const int         channelCount = ChannelCount( libSymbol );

    for( int channel = 1; channel <= channelCount; ++channel )
    {
        BOX2I buttonBox = controlButtonBox( libSymbol, bodySize, CONTROL::CHANNEL_MODE, channel );

        buttonBox = aSymbol->GetTransform().TransformCoordinate( buttonBox );
        buttonBox.Normalize();
        buttonBox.Offset( aSymbol->GetPosition() );
        buttonBox.Inflate( schIUScale.MilsToIU( 5 ) );

        if( buttonBox.Contains( aPosition ) )
            return { CONTROL::CHANNEL_MODE, channel };
    }

    for( CONTROL control : { CONTROL::ADD_CHANNEL, CONTROL::REMOVE_CHANNEL } )
    {
        BOX2I buttonBox = controlButtonBox( libSymbol, bodySize, control, 0 );

        buttonBox = aSymbol->GetTransform().TransformCoordinate( buttonBox );
        buttonBox.Normalize();
        buttonBox.Offset( aSymbol->GetPosition() );
        buttonBox.Inflate( schIUScale.MilsToIU( 5 ) );

        if( buttonBox.Contains( aPosition ) )
            return { control, 0 };
    }

    return {};
}


bool SCH_SCOPE::HitTestChannelModeControl( const SCH_SYMBOL* aSymbol, const VECTOR2I& aPosition )
{
    return HitTestControl( aSymbol, aPosition ).control == CONTROL::CHANNEL_MODE;
}


bool SCH_SCOPE::CycleChannelMode( SCH_SYMBOL* aSymbol )
{
    return CycleChannelMode( aSymbol, 1 );
}


bool SCH_SCOPE::CycleChannelMode( SCH_SYMBOL* aSymbol, int aChannel )
{
    if( !IsScopeSymbol( aSymbol ) || !aSymbol->GetLibSymbolRef() )
        return false;

    LIB_SYMBOL*    libSymbol = aSymbol->GetLibSymbolRef().get();
    const VECTOR2I bodySize = scopeBodySize( libSymbol );

    if( aChannel < 1 || aChannel > ChannelCount( libSymbol ) )
        return false;

    UpdateChannelModeControl( libSymbol, bodySize );

    SCH_TEXT* modeText = findControlText( libSymbol, CONTROL::CHANNEL_MODE, aChannel );

    if( !modeText )
        return false;

    modeText->SetText( nextChannelModeText( modeText->GetText() ) );
    return true;
}


bool SCH_SCOPE::AddChannel( SCH_SYMBOL* aSymbol )
{
    if( !IsScopeSymbol( aSymbol ) || !aSymbol->GetLibSymbolRef() )
        return false;

    LIB_SYMBOL* libSymbol = aSymbol->GetLibSymbolRef().get();
    const int   channelCount = ChannelCount( libSymbol );

    if( channelCount >= MaxChannelCount() )
        return false;

    const int newChannel = channelCount + 1;
    int       previousNegativeY = 0;

    if( !getPinY( libSymbol, channelCount, false, previousNegativeY ) )
    {
        const VECTOR2I bodySize = scopeBodySize( libSymbol );
        previousNegativeY = fallbackPinY( bodySize, channelCount, channelCount, false );
    }

    const int yPositive = previousNegativeY + PinPitch();
    const int yNegative = yPositive + PinPitch();

    setPinGeometry( libSymbol, newChannel, true, yPositive );
    setPinGeometry( libSymbol, newChannel, false, yNegative );

    if( SCH_SHAPE* body = findScopeBody( libSymbol ) )
    {
        const VECTOR2I requiredSize = MinimumSize( newChannel );
        VECTOR2I  end = body->GetEnd();

        end.x = std::max( end.x, requiredSize.x );
        end.y = std::max( end.y, requiredSize.y );
        body->SetEnd( end );
    }

    UpdateChannelGeometry( libSymbol, scopeBodySize( libSymbol ) );
    aSymbol->UpdatePins();
    return true;
}


bool SCH_SCOPE::RemoveChannel( SCH_SYMBOL* aSymbol )
{
    if( !IsScopeSymbol( aSymbol ) || !aSymbol->GetLibSymbolRef() )
        return false;

    LIB_SYMBOL* libSymbol = aSymbol->GetLibSymbolRef().get();
    const int   channelCount = ChannelCount( libSymbol );

    if( channelCount <= 1 )
        return false;

    removePinsByNumber( libSymbol, channelPinNumber( channelCount, true ) );
    removePinsByNumber( libSymbol, channelPinNumber( channelCount, false ) );

    UpdateChannelGeometry( libSymbol, scopeBodySize( libSymbol ) );
    aSymbol->UpdatePins();
    return true;
}


int SCH_SCOPE::MaxChannelCount()
{
    return 8;
}


VECTOR2I SCH_SCOPE::FirstPinOffset()
{
    return VECTOR2I( -PinLength(), channelTopY() );
}


int SCH_SCOPE::PinLength()
{
    return schIUScale.MilsToIU( 100 );
}


int SCH_SCOPE::PinPitch()
{
    return schIUScale.MilsToIU( 100 );
}
#endif


int SCH_SCOPE::GridSize()
{
    return schIUScale.MilsToIU( 50 );
}


#if 0
int SCH_SCOPE::PinTextSize()
{
    return schIUScale.mmToIU( 0.8 );
}


int SCH_SCOPE::PinNameOffset()
{
    return schIUScale.mmToIU( 0.05 );
}


int SCH_SCOPE::ChannelModeTextSize()
{
    return PinTextSize() * 80 / 100;
}


int SCH_SCOPE::ChannelModeButtonSize()
{
    return GridSize() * 90 / 100;
}
#endif


bool SCH_SCOPE::NormalizeCanvasSymbol( SCH_SYMBOL* aSymbol )
{
    if( !IsScopeSymbol( aSymbol ) || !aSymbol->GetLibSymbolRef() )
        return false;

    LIB_SYMBOL* libSymbol = aSymbol->GetLibSymbolRef().get();
    SCH_SHAPE*  body = findScopeBody( libSymbol );
    bool        changed = false;

    if( !body )
    {
        body = new SCH_SCOPE();
        libSymbol->AddDrawItem( body );
        changed = true;
    }

    std::vector<SCH_ITEM*> deprecatedItems;

    for( SCH_ITEM& item : libSymbol->GetDrawItems() )
    {
        if( &item == body )
            continue;

        if( item.Type() == SCH_PIN_T || item.Type() == SCH_TEXT_T || item.Type() == SCH_SHAPE_T )
            deprecatedItems.push_back( &item );
    }

    for( SCH_ITEM* item : deprecatedItems )
    {
        libSymbol->RemoveDrawItem( item );
        changed = true;
    }

    const SETTINGS settings = GetSettings( aSymbol );
    const STROKE_PARAMS stroke( settings.borderWidth, LINE_STYLE::SOLID,
                                settings.borderColor );

    if( body->GetFillMode() != FILL_T::FILLED_WITH_COLOR
        || body->GetFillColor() != settings.backgroundColor || body->GetStroke() != stroke
        || body->GetLayer() != LAYER_DEVICE )
    {
        body->SetFillMode( FILL_T::FILLED_WITH_COLOR );
        body->SetFillColor( settings.backgroundColor );
        body->SetStroke( stroke );
        body->SetLayer( LAYER_DEVICE );
        changed = true;
    }

    libSymbol->SetShowPinNames( false );
    libSymbol->SetShowPinNumbers( false );
    aSymbol->UpdatePins();

    return changed;
}


SCH_SCOPE::SETTINGS SCH_SCOPE::DefaultSettings()
{
    SETTINGS settings;
    settings.backgroundColor = KIGFX::COLOR4D( WHITE );
    settings.borderColor = KIGFX::COLOR4D( BLACK );
    settings.borderWidth = schIUScale.mmToIU( 0.2 );
    settings.gridVisible = true;
    settings.gridStyle = LINE_STYLE::SOLID;
    settings.gridColor = colorFromHex( "#D0D0D0" );
    settings.gridWidth = schIUScale.mmToIU( 0.1 );
    settings.minorGridVisible = true;
    settings.minorGridStyle = LINE_STYLE::SOLID;
    settings.minorGridColor = colorFromHex( "#E8E8E8" );
    settings.minorGridWidth = schIUScale.mmToIU( 0.05 );
    settings.axisFontName = wxEmptyString;
    settings.axisTextSize = schIUScale.mmToIU( 0.8 );
    return settings;
}


KIGFX::COLOR4D SCH_SCOPE::DefaultWaveformColor( size_t aIndex )
{
    static const std::vector<KIGFX::COLOR4D> colors = {
        colorFromHex( "#E41A1C" ), colorFromHex( "#377EB8" ),
        colorFromHex( "#4DAF4A" ), colorFromHex( "#984EA3" ),
        colorFromHex( "#FF7F00" ), colorFromHex( "#FFFF33" ),
        colorFromHex( "#A65628" ), colorFromHex( "#F781BF" ),
        colorFromHex( "#66C2A5" ), colorFromHex( "#FC8D62" ),
        colorFromHex( "#8DA0CB" ), colorFromHex( "#E78AC3" ),
        colorFromHex( "#A6D854" ), colorFromHex( "#FFD92F" ),
        colorFromHex( "#E5C494" ), colorFromHex( "#B3B3B3" )
    };

    return colors[aIndex % colors.size()];
}


SCH_SCOPE::SETTINGS SCH_SCOPE::GetSettings( const SCH_SYMBOL* aSymbol )
{
    if( !IsScopeSymbol( aSymbol ) )
        return DefaultSettings();

    const SCH_FIELD* field = aSymbol->GetField( SCOPE_WAVEFORMS_FIELD );
    const wxString   serialized = field ? field->GetText() : wxString();
    auto             cached = s_scopeSettingsCache.find( aSymbol->m_Uuid );

    if( cached != s_scopeSettingsCache.end() && cached->second.serialized == serialized )
        return cached->second.settings;

    SETTINGS settings = parseSettings( serialized );
    s_scopeSettingsCache[aSymbol->m_Uuid] = { serialized, settings };
    return settings;
}


void SCH_SCOPE::SetSettings( SCH_SYMBOL* aSymbol, const SETTINGS& aSettings )
{
    if( !IsScopeSymbol( aSymbol ) )
        return;

    const SETTINGS previous = GetSettings( aSymbol );
    const SETTINGS settings = sanitizeSettings( aSettings );
    SCH_FIELD*     field = aSymbol->GetField( SCOPE_WAVEFORMS_FIELD );

    if( !field )
    {
        SCH_FIELD newField( aSymbol, FIELD_T::USER, SCOPE_WAVEFORMS_FIELD );
        newField.SetOrdinal( aSymbol->GetNextFieldOrdinal() );
        newField.SetVisible( false );
        field = aSymbol->AddField( newField );
    }

    const wxString serialized = serializeSettings( settings );
    field->SetText( serialized );
    field->SetVisible( false );
    s_scopeSettingsCache[aSymbol->m_Uuid] = { serialized, settings };

    if( LIB_SYMBOL* libSymbol = aSymbol->GetLibSymbolRef().get() )
    {
        if( SCH_SHAPE* body = findScopeBody( libSymbol ) )
        {
            body->SetFillMode( FILL_T::FILLED_WITH_COLOR );
            body->SetFillColor( settings.backgroundColor );
            body->SetStroke( STROKE_PARAMS( settings.borderWidth, LINE_STYLE::SOLID,
                                            settings.borderColor ) );
        }
    }

    if( !sameSources( previous, settings ) )
    {
        auto runtime = s_scopeRuntime.find( aSymbol->m_Uuid );

        if( runtime != s_scopeRuntime.end() )
        {
            updateCursorIntersections( settings, runtime->second );
            updateCursorMeasurement( aSymbol, settings, runtime->second );
        }
    }
}


std::vector<wxString> SCH_SCOPE::GetWaveformSources( const SCH_SYMBOL* aSymbol )
{
    std::vector<wxString> sources;

    for( const WAVEFORM_SOURCE& source : GetSettings( aSymbol ).sources )
        sources.push_back( source.name );

    return sources;
}


void SCH_SCOPE::SetWaveformSources( SCH_SYMBOL* aSymbol, const std::vector<wxString>& aSources )
{
    if( !IsScopeSymbol( aSymbol ) )
        return;

    SETTINGS settings = GetSettings( aSymbol );
    std::vector<WAVEFORM_SOURCE> sources;

    for( const wxString& source : aSources )
    {
        if( source.IsEmpty() )
            continue;

        auto existing = std::find_if( settings.sources.begin(), settings.sources.end(),
                                      [&]( const WAVEFORM_SOURCE& aExisting )
                                      {
                                          return aExisting.name == source;
                                      } );

        if( existing != settings.sources.end() )
            sources.push_back( *existing );
        else
            sources.push_back( { source, DefaultWaveformColor( sources.size() ),
                                 schIUScale.mmToIU( 0.2 ) } );
    }

    settings.sources = std::move( sources );
    SetSettings( aSymbol, settings );
}


const std::vector<SCH_SCOPE::WAVEFORM>* SCH_SCOPE::GetWaveforms( const SCH_SYMBOL* aSymbol )
{
    if( !aSymbol )
        return nullptr;

    auto it = s_scopeRuntime.find( aSymbol->m_Uuid );
    return it == s_scopeRuntime.end() ? nullptr : &it->second.waveforms;
}


void SCH_SCOPE::SetWaveforms( const SCH_SYMBOL* aSymbol, std::vector<WAVEFORM> aWaveforms )
{
    if( !aSymbol )
        return;

    for( WAVEFORM& waveform : aWaveforms )
    {
        const size_t size = std::min( waveform.x.size(), waveform.y.size() );
        waveform.minX = std::numeric_limits<double>::infinity();
        waveform.maxX = -std::numeric_limits<double>::infinity();
        waveform.minY = std::numeric_limits<double>::infinity();
        waveform.maxY = -std::numeric_limits<double>::infinity();
        waveform.monotonicX = true;
        double previousX = -std::numeric_limits<double>::infinity();

        for( size_t ii = 0; ii < size; ++ii )
        {
            double& x = waveform.x[ii];
            double& y = waveform.y[ii];

            if( !std::isfinite( x ) || !std::isfinite( y ) )
            {
                if( !std::isfinite( x ) )
                    waveform.monotonicX = false;

                continue;
            }

            x = clipScopeNearZero( x );
            y = clipScopeNearZero( y );

            waveform.minX = std::min( waveform.minX, x );
            waveform.maxX = std::max( waveform.maxX, x );
            waveform.minY = std::min( waveform.minY, y );
            waveform.maxY = std::max( waveform.maxY, y );
            waveform.monotonicX = waveform.monotonicX && x >= previousX;
            previousX = x;
        }

        if( !std::isfinite( waveform.minX ) )
        {
            waveform.minX = 0.0;
            waveform.maxX = 1.0;
            waveform.minY = 0.0;
            waveform.maxY = 1.0;
        }

        if( waveform.maxX == waveform.minX )
            waveform.maxX = waveform.minX + 1.0;

        if( waveform.maxY == waveform.minY )
        {
            waveform.minY -= 0.5;
            waveform.maxY += 0.5;
        }
    }

    DATA_BOUNDS bounds;
    bool        haveBounds = false;

    for( const WAVEFORM& waveform : aWaveforms )
    {
        if( !haveBounds )
        {
            bounds = { waveform.minX, waveform.maxX, waveform.minY, waveform.maxY };
            haveBounds = true;
        }
        else
        {
            bounds.minX = std::min( bounds.minX, waveform.minX );
            bounds.maxX = std::max( bounds.maxX, waveform.maxX );
            bounds.minY = std::min( bounds.minY, waveform.minY );
            bounds.maxY = std::max( bounds.maxY, waveform.maxY );
        }
    }

    if( haveBounds )
    {
        const double yPadding = ( bounds.maxY - bounds.minY ) * SCOPE_VERTICAL_PADDING;
        bounds.minY -= yPadding;
        bounds.maxY += yPadding;
    }

    SCOPE_RUNTIME_STATE& runtime = s_scopeRuntime[aSymbol->m_Uuid];
    runtime.waveforms = std::move( aWaveforms );
    runtime.bounds = bounds;
    runtime.zoomSelection = ZOOM_SELECTION();
    const SETTINGS settings = GetSettings( aSymbol );
    updateCursorIntersections( settings, runtime );
    updateCursorMeasurement( aSymbol, settings, runtime );
}


void SCH_SCOPE::ClearWaveforms( const SCH_SYMBOL* aSymbol )
{
    if( !aSymbol )
        return;

    SCOPE_RUNTIME_STATE& runtime = s_scopeRuntime[aSymbol->m_Uuid];
    runtime.waveforms.clear();
    runtime.bounds = DATA_BOUNDS();
    runtime.zoomSelection = ZOOM_SELECTION();
    const SETTINGS settings = GetSettings( aSymbol );
    updateCursorIntersections( settings, runtime );
    updateCursorMeasurement( aSymbol, settings, runtime );
}


SCH_SCOPE::DATA_BOUNDS SCH_SCOPE::GetDataBounds( const SCH_SYMBOL* aSymbol )
{
    if( !aSymbol )
        return {};

    auto it = s_scopeRuntime.find( aSymbol->m_Uuid );
    return it == s_scopeRuntime.end() ? DATA_BOUNDS() : it->second.bounds;
}


SCH_SCOPE::AXIS_INFO SCH_SCOPE::GetAxisInfo( const SCH_SYMBOL* aSymbol )
{
    if( !aSymbol )
        return {};

    auto it = s_scopeRuntime.find( aSymbol->m_Uuid );
    return it == s_scopeRuntime.end() ? AXIS_INFO() : it->second.axisInfo;
}


void SCH_SCOPE::SetAxisInfo( const SCH_SYMBOL* aSymbol, const AXIS_INFO& aInfo )
{
    if( aSymbol )
        s_scopeRuntime[aSymbol->m_Uuid].axisInfo = aInfo;
}


std::vector<SCH_SCOPE::CURSOR> SCH_SCOPE::GetCursors( const SCH_SYMBOL* aSymbol )
{
    if( !aSymbol )
        return {};

    auto runtime = s_scopeRuntime.find( aSymbol->m_Uuid );

    if( runtime == s_scopeRuntime.end() )
        return {};

    const SETTINGS settings = GetSettings( aSymbol );
    const wxString topSource = settings.sources.empty() ? wxString()
                                                         : settings.sources.front().name;

    if( runtime->second.cursorSourceName != topSource )
    {
        updateCursorIntersections( settings, runtime->second );
        updateCursorMeasurement( aSymbol, settings, runtime->second );
    }

    return runtime->second.cursors;
}


int SCH_SCOPE::AddCursor( const SCH_SYMBOL* aSymbol, double aNormalizedX )
{
    if( !aSymbol || !std::isfinite( aNormalizedX ) )
        return -1;

    SCOPE_RUNTIME_STATE& runtime = s_scopeRuntime[aSymbol->m_Uuid];
    const SETTINGS      settings = GetSettings( aSymbol );

    if( settings.sources.empty() || runtime.cursors.size() >= 2 )
        return -1;

    const auto waveform = std::find_if(
            runtime.waveforms.begin(), runtime.waveforms.end(),
            [&]( const WAVEFORM& aWaveform )
            {
                return aWaveform.name == settings.sources.front().name
                       && std::min( aWaveform.x.size(), aWaveform.y.size() ) >= 2;
            } );

    if( waveform == runtime.waveforms.end() )
        return -1;

    runtime.cursors.push_back( { std::clamp( aNormalizedX, 0.0, 1.0 ), 0.0, false } );
    updateCursorIntersections( settings, runtime );
    updateCursorReadout( aSymbol, settings, runtime );
    return static_cast<int>( runtime.cursors.size() ) - 1;
}


bool SCH_SCOPE::MoveCursor( const SCH_SYMBOL* aSymbol, int aIndex, double aNormalizedX )
{
    if( !aSymbol || !std::isfinite( aNormalizedX ) )
        return false;

    SCOPE_RUNTIME_STATE& runtime = s_scopeRuntime[aSymbol->m_Uuid];

    if( aIndex < 0 || aIndex >= static_cast<int>( runtime.cursors.size() ) )
        return false;

    CURSOR& cursor = runtime.cursors[aIndex];
    const double nextX = std::clamp( aNormalizedX, 0.0, 1.0 );

    if( std::abs( cursor.x - nextX ) < 1e-9 )
        return false;

    cursor.x = nextX;
    const SETTINGS settings = GetSettings( aSymbol );
    updateCursorIntersections( settings, runtime );
    updateCursorReadout( aSymbol, settings, runtime );
    return true;
}


bool SCH_SCOPE::RemoveCursor( const SCH_SYMBOL* aSymbol, int aIndex )
{
    if( !aSymbol )
        return false;

    SCOPE_RUNTIME_STATE& runtime = s_scopeRuntime[aSymbol->m_Uuid];

    if( aIndex < 0 || aIndex >= static_cast<int>( runtime.cursors.size() ) )
        return false;

    runtime.cursors.erase( runtime.cursors.begin() + aIndex );
    updateCursorMeasurement( aSymbol, GetSettings( aSymbol ), runtime );
    return true;
}


void SCH_SCOPE::BeginCursorMove( const SCH_SYMBOL* aSymbol )
{
    if( !aSymbol )
        return;

    SCOPE_RUNTIME_STATE& runtime = s_scopeRuntime[aSymbol->m_Uuid];
    updateCursorReadout( aSymbol, GetSettings( aSymbol ), runtime );
}


void SCH_SCOPE::FinishCursorMove( const SCH_SYMBOL* aSymbol )
{
    if( !aSymbol )
        return;

    SCOPE_RUNTIME_STATE& runtime = s_scopeRuntime[aSymbol->m_Uuid];
    updateCursorMeasurement( aSymbol, GetSettings( aSymbol ), runtime );
}


SCH_SCOPE::CURSOR_MEASUREMENT SCH_SCOPE::GetCursorMeasurement( const SCH_SYMBOL* aSymbol )
{
    if( !aSymbol )
        return {};

    auto runtime = s_scopeRuntime.find( aSymbol->m_Uuid );

    if( runtime == s_scopeRuntime.end() )
        return {};

    const SETTINGS settings = GetSettings( aSymbol );
    const wxString topSource = settings.sources.empty() ? wxString()
                                                         : settings.sources.front().name;

    if( runtime->second.cursorSourceName != topSource )
    {
        updateCursorIntersections( settings, runtime->second );
        updateCursorMeasurement( aSymbol, settings, runtime->second );
    }

    return runtime->second.cursorMeasurement;
}


bool SCH_SCOPE::CursorXToPlot( const CURSOR& aCursor, const VIEWPORT& aViewport,
                               const BOX2I& aPlotBox, int& aPosition )
{
    const double xRange = aViewport.xMax - aViewport.xMin;

    if( !aCursor.valid || xRange <= 0.0 || aCursor.x < aViewport.xMin
        || aCursor.x > aViewport.xMax || aPlotBox.GetWidth() <= 0 )
    {
        return false;
    }

    aPosition = KiROUND( aPlotBox.GetX()
                         + ( aCursor.x - aViewport.xMin ) / xRange
                                   * aPlotBox.GetWidth() );
    return true;
}


bool SCH_SCOPE::CursorToPlot( const CURSOR& aCursor, const VIEWPORT& aViewport,
                              const BOX2I& aPlotBox, VECTOR2I& aPosition )
{
    const double yRange = aViewport.yMax - aViewport.yMin;

    if( !CursorXToPlot( aCursor, aViewport, aPlotBox, aPosition.x ) || yRange <= 0.0
        || aCursor.y < aViewport.yMin || aCursor.y > aViewport.yMax
        || aPlotBox.GetHeight() <= 0 )
    {
        return false;
    }

    aPosition.y = KiROUND( aPlotBox.GetEnd().y
                           - ( aCursor.y - aViewport.yMin ) / yRange
                                     * aPlotBox.GetHeight() );
    return true;
}


wxString SCH_SCOPE::FormatEngineeringValue( double aValue )
{
    if( !std::isfinite( aValue ) )
        return wxString::Format( wxS( "%.4g" ), aValue );

    if( std::abs( aValue ) < SCOPE_ZERO_CLIP )
        return wxS( "0" );

    constexpr int minExponent = -15;
    constexpr int maxExponent = 12;
    int exponent = static_cast<int>( std::floor( std::log10( std::abs( aValue ) ) / 3.0 ) )
                   * 3;
    exponent = std::clamp( exponent, minExponent, maxExponent );
    double scaled = aValue / std::pow( 10.0, exponent );

    // Four significant digits would round this to 1000, so advance the prefix instead.
    if( std::abs( scaled ) >= 999.95 && exponent < maxExponent )
    {
        scaled /= 1000.0;
        exponent += 3;
    }

    static constexpr std::array<const char*, 10> prefixes = {
        "f", "p", "n", "u", "m", "", "k", "M", "G", "T"
    };

    wxString number = wxString::Format( wxS( "%.4g" ), scaled );

    if( number == wxS( "-0" ) )
        number = wxS( "0" );

    return number + wxString::FromUTF8( prefixes[( exponent - minExponent ) / 3] );
}


wxString SCH_SCOPE::FormatWaveformLabel( const wxString& aSource )
{
    wxString label;
    size_t   offset = 0;

    while( offset < aSource.length() )
    {
        const size_t voltageStart = aSource.find( wxS( "V(" ), offset );

        if( voltageStart == wxString::npos )
        {
            label += aSource.Mid( offset );
            break;
        }

        const size_t nodeEnd = aSource.find( wxS( ")" ), voltageStart + 2 );

        if( nodeEnd == wxString::npos )
        {
            label += aSource.Mid( offset );
            break;
        }

        label += aSource.Mid( offset, voltageStart + 2 - offset );
        label += compactScopeNetName(
                aSource.Mid( voltageStart + 2, nodeEnd - voltageStart - 2 ) );
        label += wxS( ")" );
        offset = nodeEnd + 1;
    }

    return label.IsEmpty() ? aSource : label;
}


wxString SCH_SCOPE::FitLegendLabel( const wxString& aLabel, int aAvailableWidth,
                                    int aTextSize )
{
    if( aLabel.IsEmpty() || aAvailableWidth <= 0 || aTextSize <= 0 )
        return wxEmptyString;

    const int maxChars = aAvailableWidth / std::max( 1, aTextSize );

    if( maxChars <= 0 )
        return wxEmptyString;

    if( static_cast<int>( aLabel.length() ) <= maxChars )
        return aLabel;

    if( maxChars <= 3 )
        return wxString( wxS( "..." ) ).Left( maxChars );

    return aLabel.Left( maxChars - 3 ) + wxS( "..." );
}


std::vector<wxString> SCH_SCOPE::FormatEngineeringTicks( double aMin, double aMax,
                                                         int aDivisions )
{
    aDivisions = std::max( 1, aDivisions );
    std::vector<wxString> labels;
    labels.reserve( static_cast<size_t>( aDivisions + 1 ) );

    for( int division = 0; division <= aDivisions; ++division )
    {
        const double value = aMin + ( aMax - aMin ) * division / aDivisions;
        labels.push_back( FormatEngineeringValue( value ) );
    }

    bool hasDuplicate = false;

    for( size_t i = 1; i < labels.size(); ++i )
    {
        if( labels[i] == labels[i - 1] )
        {
            hasDuplicate = true;
            break;
        }
    }

    if( !hasDuplicate )
        return labels;

    // At deep zoom the absolute values can share all four displayed significant digits.
    // Keep the first tick as the offset and express the remaining ticks relative to it, which
    // naturally selects a smaller engineering prefix without filling the plot with zeroes.
    labels.front() = FormatEngineeringValue( aMin );

    for( int division = 1; division <= aDivisions; ++division )
    {
        const double delta = ( aMax - aMin ) * division / aDivisions;
        labels[division] = FormatEngineeringValue( delta );

        if( delta > 0.0 && labels[division] != wxS( "0" ) )
            labels[division].Prepend( wxS( "+" ) );
    }

    return labels;
}


SCH_SCOPE::LAYOUT SCH_SCOPE::GetLayout( const SCH_SYMBOL* aSymbol )
{
    LAYOUT layout;

    if( !aSymbol )
        return layout;

    layout.bodyBox = aSymbol->GetBodyBoundingBox();
    layout.bodyBox.Normalize();
    const int width = static_cast<int>( layout.bodyBox.GetWidth() );
    const int height = static_cast<int>( layout.bodyBox.GetHeight() );

    if( width <= 0 || height <= 0 )
        return layout;

    const SETTINGS settings = GetSettings( aSymbol );
    layout.margin = std::max( schIUScale.MilsToIU( 16 ), std::min( width, height ) / 32 );
    layout.textSize = std::clamp( settings.axisTextSize, schIUScale.mmToIU( 0.25 ),
                                  std::max( schIUScale.mmToIU( 0.25 ), height / 12 ) );
    layout.lineHeight = layout.textSize * 3 / 2;
    layout.plotBox = layout.bodyBox;
    layout.plotBox.Inflate( -layout.margin );
    const int gridCell = schIUScale.MilsToIU( 50 );
    const int leftAxisMargin = layout.textSize * 6;
    const int rightAxisMargin = std::max( 0, layout.textSize * 2 - gridCell );
    const int bottomAxisMargin = std::max( layout.textSize * 3,
                                           layout.textSize * 4 - gridCell * 4 / 5 );
    layout.plotBox.SetX( layout.plotBox.GetX() + leftAxisMargin );
    layout.plotBox.SetWidth( layout.plotBox.GetWidth()
                             - leftAxisMargin - rightAxisMargin );

    const int sourceCount = static_cast<int>( settings.sources.size() );
    const int legendGridGap = sourceCount > 0 ? KiROUND( height * 0.025 ) : 0;
    layout.legendTop = layout.plotBox.GetY();

    if( sourceCount > 0 )
    {
        size_t longestLabel = 0;

        for( const WAVEFORM_SOURCE& source : settings.sources )
            longestLabel = std::max( longestLabel, FormatWaveformLabel( source.name ).length() );

        const int swatchWidth = layout.textSize * 2;
        const int estimatedLabelWidth = static_cast<int>( longestLabel ) * layout.textSize;
        const int desiredColumnWidth = std::max( layout.textSize * 6,
                                                 swatchWidth + estimatedLabelWidth
                                                         + layout.textSize );
        const int fittingColumns = std::max(
                1, static_cast<int>( layout.plotBox.GetWidth() )
                           / std::max( 1, desiredColumnWidth ) );
        const int maxLegendRows = std::max(
                1, height / std::max( 1, layout.lineHeight * 5 ) );
        layout.legendColumns = std::min( sourceCount, fittingColumns );
        layout.legendRows = ( sourceCount + layout.legendColumns - 1 )
                            / layout.legendColumns;

        if( layout.legendRows > maxLegendRows )
        {
            layout.legendRows = maxLegendRows;
            layout.legendColumns = ( sourceCount + layout.legendRows - 1 )
                                   / layout.legendRows;
        }
    }

    layout.plotBox.SetY( layout.plotBox.GetY()
                         + layout.legendRows * layout.lineHeight + legendGridGap );
    layout.plotBox.SetHeight( layout.plotBox.GetHeight()
                              - layout.legendRows * layout.lineHeight - legendGridGap
                              - bottomAxisMargin );
    layout.xDivisions = std::clamp( static_cast<int>( layout.plotBox.GetWidth()
                                                       / std::max( 1, layout.textSize * 7 ) ),
                                    2, 5 );
    layout.yDivisions = std::clamp( static_cast<int>( layout.plotBox.GetHeight()
                                                       / std::max( 1, layout.textSize * 3 ) ),
                                    2, 5 );
    return layout;
}


int SCH_SCOPE::HitTestLegend( const SCH_SYMBOL* aSymbol, const VECTOR2I& aPosition )
{
    if( !IsScopeSymbol( aSymbol ) )
        return -1;

    const LAYOUT   layout = GetLayout( aSymbol );
    const SETTINGS settings = GetSettings( aSymbol );

    if( layout.legendRows <= 0 || settings.sources.empty() )
        return -1;

    const int columnWidth = std::max( 1, static_cast<int>( layout.plotBox.GetWidth() )
                                            / layout.legendColumns );

    for( size_t index = 0; index < settings.sources.size(); ++index )
    {
        const int column = static_cast<int>( index ) % layout.legendColumns;
        const int row = static_cast<int>( index ) / layout.legendColumns;
        BOX2I entryBox( VECTOR2I( layout.plotBox.GetX() + column * columnWidth,
                                  layout.legendTop + row * layout.lineHeight ),
                        VECTOR2I( columnWidth, layout.lineHeight ) );

        if( entryBox.Contains( aPosition ) )
            return static_cast<int>( index );
    }

    return -1;
}


std::vector<std::pair<VECTOR2I, VECTOR2I>> SCH_SCOPE::BuildWaveformSegments(
        const WAVEFORM& aWaveform, const DATA_BOUNDS& aBounds, const VIEWPORT& aViewport,
        const BOX2I& aPlotBox, size_t aBucketCount )
{
    std::vector<std::pair<VECTOR2I, VECTOR2I>> segments;
    const size_t size = std::min( aWaveform.x.size(), aWaveform.y.size() );
    const double xRange = aBounds.maxX - aBounds.minX;
    const double yRange = aBounds.maxY - aBounds.minY;
    const double xViewportRange = aViewport.xMax - aViewport.xMin;
    const double yViewportRange = aViewport.yMax - aViewport.yMin;

    if( size < 2 || xRange <= 0.0 || yRange <= 0.0 || xViewportRange <= 0.0
        || yViewportRange <= 0.0 || aPlotBox.GetWidth() <= 0 || aPlotBox.GetHeight() <= 0 )
    {
        return segments;
    }

    const double visibleMinX = aBounds.minX + aViewport.xMin * xRange;
    const double visibleMaxX = aBounds.minX + aViewport.xMax * xRange;
    size_t first = 0;
    size_t last = size;

    if( aWaveform.monotonicX )
    {
        auto begin = aWaveform.x.begin();
        auto end = begin + static_cast<ptrdiff_t>( size );
        first = static_cast<size_t>( std::lower_bound( begin, end, visibleMinX ) - begin );
        last = static_cast<size_t>( std::upper_bound( begin, end, visibleMaxX ) - begin );

        if( first > 0 )
            --first;

        if( last < size )
            ++last;
    }

    aBucketCount = std::max<size_t>( 1, aBucketCount );
    std::vector<size_t> indices;
    indices.reserve( std::min<size_t>( last - first, aBucketCount * 4 + 2 ) );

    if( !aWaveform.monotonicX || last - first <= aBucketCount * 4 )
    {
        const size_t stride = aWaveform.monotonicX
                                      ? 1
                                      : std::max<size_t>( 1, ( last - first )
                                                                   / ( aBucketCount * 2 ) );

        for( size_t ii = first; ii < last; ii += stride )
            indices.push_back( ii );

        if( last > first && ( indices.empty() || indices.back() != last - 1 ) )
            indices.push_back( last - 1 );
    }
    else
    {
        size_t bucketFirst = first;
        size_t bucketMin = first;
        size_t bucketMax = first;
        size_t bucketLast = first;
        int    currentBucket = -1;

        auto flushBucket = [&]()
        {
            std::array<size_t, 4> candidates = { bucketFirst, bucketMin, bucketMax, bucketLast };
            std::sort( candidates.begin(), candidates.end() );

            for( size_t index : candidates )
            {
                if( indices.empty() || indices.back() != index )
                    indices.push_back( index );
            }
        };

        for( size_t ii = first; ii < last; ++ii )
        {
            if( !std::isfinite( aWaveform.x[ii] ) || !std::isfinite( aWaveform.y[ii] ) )
                continue;

            const double normalizedX = ( aWaveform.x[ii] - aBounds.minX ) / xRange;
            const int bucket = std::clamp(
                    static_cast<int>( ( normalizedX - aViewport.xMin ) / xViewportRange
                                      * aBucketCount ),
                    0, static_cast<int>( aBucketCount ) - 1 );

            if( currentBucket != bucket )
            {
                if( currentBucket >= 0 )
                    flushBucket();

                currentBucket = bucket;
                bucketFirst = bucketMin = bucketMax = bucketLast = ii;
            }
            else
            {
                if( aWaveform.y[ii] < aWaveform.y[bucketMin] )
                    bucketMin = ii;

                if( aWaveform.y[ii] > aWaveform.y[bucketMax] )
                    bucketMax = ii;

                bucketLast = ii;
            }
        }

        if( currentBucket >= 0 )
            flushBucket();
    }

    bool     havePrevious = false;
    VECTOR2I previous;

    for( size_t index : indices )
    {
        if( !std::isfinite( aWaveform.x[index] ) || !std::isfinite( aWaveform.y[index] ) )
        {
            havePrevious = false;
            continue;
        }

        const double normalizedX = ( aWaveform.x[index] - aBounds.minX ) / xRange;
        const double normalizedY = ( aWaveform.y[index] - aBounds.minY ) / yRange;
        VECTOR2I point( KiROUND( aPlotBox.GetX()
                                 + ( normalizedX - aViewport.xMin ) / xViewportRange
                                           * aPlotBox.GetWidth() ),
                        KiROUND( aPlotBox.GetEnd().y
                                 - ( normalizedY - aViewport.yMin ) / yViewportRange
                                           * aPlotBox.GetHeight() ) );

        if( havePrevious )
        {
            int x1 = previous.x;
            int y1 = previous.y;
            int x2 = point.x;
            int y2 = point.y;

            if( !ClipLine( &aPlotBox, x1, y1, x2, y2 ) )
                segments.emplace_back( VECTOR2I( x1, y1 ), VECTOR2I( x2, y2 ) );
        }

        previous = point;
        havePrevious = true;
    }

    return segments;
}


void SCH_SCOPE::PlotWaveforms( PLOTTER* aPlotter, const SCH_SYMBOL* aSymbol )
{
    if( !aPlotter || !IsScopeSymbol( aSymbol ) )
        return;

    const LAYOUT layout = GetLayout( aSymbol );

    if( layout.plotBox.GetWidth() <= 0 || layout.plotBox.GetHeight() <= 0 )
        return;

    const SETTINGS settings = GetSettings( aSymbol );
    const VIEWPORT viewport = GetViewport( aSymbol );
    const DATA_BOUNDS bounds = GetDataBounds( aSymbol );
    const AXIS_INFO axisInfo = GetAxisInfo( aSymbol );
    const std::vector<WAVEFORM>* waveforms = GetWaveforms( aSymbol );

    auto drawGrid = [&]( int aXDivisions, int aYDivisions, bool aSkipMajor,
                         const KIGFX::COLOR4D& aColor, int aWidth, LINE_STYLE aStyle )
    {
        aPlotter->SetColor( aColor );
        aPlotter->SetDash( aWidth, aStyle );

        for( int division = 1; division < aXDivisions; ++division )
        {
            if( aSkipMajor && division % 5 == 0 )
                continue;

            const int x = layout.plotBox.GetX()
                          + layout.plotBox.GetWidth() * division / aXDivisions;
            aPlotter->ThickSegment( VECTOR2I( x, layout.plotBox.GetY() ),
                                    VECTOR2I( x, layout.plotBox.GetEnd().y ), aWidth, nullptr );
        }

        for( int division = 1; division < aYDivisions; ++division )
        {
            if( aSkipMajor && division % 5 == 0 )
                continue;

            const int y = layout.plotBox.GetY()
                          + layout.plotBox.GetHeight() * division / aYDivisions;
            aPlotter->ThickSegment( VECTOR2I( layout.plotBox.GetX(), y ),
                                    VECTOR2I( layout.plotBox.GetEnd().x, y ), aWidth, nullptr );
        }
    };

    if( settings.minorGridVisible )
        drawGrid( layout.xDivisions * 5, layout.yDivisions * 5, true,
                  settings.minorGridColor, settings.minorGridWidth, settings.minorGridStyle );

    if( settings.gridVisible )
        drawGrid( layout.xDivisions, layout.yDivisions, false, settings.gridColor,
                  settings.gridWidth, settings.gridStyle );

    aPlotter->SetDash( settings.gridWidth, LINE_STYLE::SOLID );
    // Scope axis text is generated at plot time and is not part of the schematic's embedded
    // font collection.  Use the stroke font for PDF so an unavailable outline font cannot
    // suppress the scope; other plot formats can still use the selected face.
    KIFONT::FONT* font = KIFONT::FONT::GetFont();

    if( aPlotter->GetPlotterType() != PLOT_FORMAT::PDF )
        font = KIFONT::FONT::GetFont( settings.axisFontName );

    TEXT_ATTRIBUTES textAttrs( font );
    textAttrs.m_Size = VECTOR2I( layout.textSize, layout.textSize );
    textAttrs.m_StrokeWidth = std::max( 1, layout.textSize / 10 );
    textAttrs.m_Color = settings.borderColor;
    const double minX = bounds.minX + viewport.xMin * ( bounds.maxX - bounds.minX );
    const double maxX = bounds.minX + viewport.xMax * ( bounds.maxX - bounds.minX );
    const double minY = bounds.minY + viewport.yMin * ( bounds.maxY - bounds.minY );
    const double maxY = bounds.minY + viewport.yMax * ( bounds.maxY - bounds.minY );
    const std::vector<wxString> xTickLabels =
            FormatEngineeringTicks( minX, maxX, layout.xDivisions );
    const std::vector<wxString> yTickLabels =
            FormatEngineeringTicks( minY, maxY, layout.yDivisions );
    int maxYTickWidth = 0;

    for( const wxString& label : yTickLabels )
    {
        const VECTOR2I extent = font->StringBoundaryLimits(
                label, textAttrs.m_Size, textAttrs.m_StrokeWidth, false, false,
                KIFONT::METRICS::Default() );
        maxYTickWidth = std::max( maxYTickWidth, extent.x );
    }

    for( int division = 0; division <= layout.xDivisions; ++division )
    {
        const int x = layout.plotBox.GetX()
                      + layout.plotBox.GetWidth() * division / layout.xDivisions;
        textAttrs.m_Halign = division == 0
                                    ? GR_TEXT_H_ALIGN_LEFT
                                    : division == layout.xDivisions ? GR_TEXT_H_ALIGN_RIGHT
                                                                    : GR_TEXT_H_ALIGN_CENTER;
        textAttrs.m_Valign = GR_TEXT_V_ALIGN_TOP;
        aPlotter->PlotText(
                VECTOR2I( x, layout.plotBox.GetEnd().y + layout.textSize * 2 / 3 ),
                settings.borderColor, xTickLabels[division],
                textAttrs, font );
    }

    for( int division = 0; division <= layout.yDivisions; ++division )
    {
        const int y = layout.plotBox.GetEnd().y
                      - layout.plotBox.GetHeight() * division / layout.yDivisions;
        textAttrs.m_Halign = GR_TEXT_H_ALIGN_RIGHT;
        textAttrs.m_Valign = GR_TEXT_V_ALIGN_CENTER;
        aPlotter->PlotText(
                VECTOR2I( layout.plotBox.GetX() - layout.textSize / 3, y ),
                settings.borderColor, yTickLabels[division],
                textAttrs, font );
    }

    textAttrs.m_Halign = GR_TEXT_H_ALIGN_CENTER;
    textAttrs.m_Valign = GR_TEXT_V_ALIGN_TOP;
    textAttrs.m_Angle = ANGLE_0;
    aPlotter->PlotText( VECTOR2I( layout.plotBox.GetCenter().x,
                                 layout.plotBox.GetEnd().y + layout.textSize * 2 ),
                        settings.borderColor, axisInfo.xName, textAttrs, font );

    const int yAxisLabelX = layout.bodyBox.GetX() + layout.margin + layout.textSize / 2;
    const int yTickLeft = layout.plotBox.GetX() - layout.textSize / 3 - maxYTickWidth;

    textAttrs.m_Valign = GR_TEXT_V_ALIGN_CENTER;

    if( yAxisLabelX + layout.textSize / 2 >= yTickLeft && layout.yDivisions > 0 )
    {
        const int tickGap = layout.plotBox.GetHeight() / layout.yDivisions;
        const int gapIndex = layout.yDivisions / 2;
        const int labelY = layout.plotBox.GetEnd().y - tickGap * gapIndex - tickGap / 2;
        textAttrs.m_Angle = ANGLE_0;
        textAttrs.m_Halign = GR_TEXT_H_ALIGN_RIGHT;
        aPlotter->PlotText( VECTOR2I( layout.plotBox.GetX() - layout.textSize / 3,
                                      labelY ),
                            settings.borderColor, axisInfo.yName, textAttrs, font );
    }
    else
    {
        textAttrs.m_Angle = ANGLE_90;
        textAttrs.m_Halign = GR_TEXT_H_ALIGN_CENTER;
        aPlotter->PlotText( VECTOR2I( yAxisLabelX, layout.plotBox.GetCenter().y ),
                            settings.borderColor, axisInfo.yName, textAttrs, font );
    }

    if( layout.legendRows > 0 )
    {
        const int columnWidth = std::max( 1, static_cast<int>( layout.plotBox.GetWidth() )
                                                  / layout.legendColumns );

        for( size_t index = 0; index < settings.sources.size(); ++index )
        {
            const WAVEFORM_SOURCE& source = settings.sources[index];
            const int column = static_cast<int>( index ) % layout.legendColumns;
            const int row = static_cast<int>( index ) / layout.legendColumns;
            const int x = layout.plotBox.GetX() + column * columnWidth;
            const int y = layout.legendTop + row * layout.lineHeight + layout.lineHeight / 2;
            const int swatchWidth = std::min( layout.textSize * 2,
                                              std::max( 0, columnWidth - layout.textSize ) );
            const int availableLabelWidth = std::max(
                    0, columnWidth - swatchWidth - layout.textSize );
            const wxString label = FitLegendLabel( FormatWaveformLabel( source.name ),
                                                   availableLabelWidth, layout.textSize );

            aPlotter->SetColor( source.color );
            aPlotter->ThickSegment( VECTOR2I( x, y ), VECTOR2I( x + swatchWidth, y ),
                                    source.lineWidth, nullptr );

            if( !label.IsEmpty() )
            {
                textAttrs.m_Angle = ANGLE_0;
                textAttrs.m_Halign = GR_TEXT_H_ALIGN_LEFT;
                textAttrs.m_Valign = GR_TEXT_V_ALIGN_CENTER;
                aPlotter->PlotText( VECTOR2I( x + swatchWidth + layout.textSize / 2, y ),
                                    settings.borderColor, label, textAttrs, font );
            }
        }
    }

    if( waveforms )
    {
        for( auto sourceIt = settings.sources.rbegin(); sourceIt != settings.sources.rend();
             ++sourceIt )
        {
            const WAVEFORM_SOURCE& source = *sourceIt;
            auto waveform = std::find_if( waveforms->begin(), waveforms->end(),
                                          [&]( const WAVEFORM& aWaveform )
                                          {
                                              return aWaveform.name == source.name;
                                          } );

            if( waveform == waveforms->end() )
                continue;

            aPlotter->SetColor( source.color );

            for( const auto& [start, end] : BuildWaveformSegments(
                         *waveform, bounds, viewport, layout.plotBox, 1200 ) )
            {
                aPlotter->ThickSegment( start, end, source.lineWidth, nullptr );
            }
        }
    }

    aPlotter->SetColor( KIGFX::COLOR4D( BLACK ) );
    const int cursorWidth = std::max( 1, settings.minorGridWidth );
    const int cursorDiameter = cursorWidth * 2;
    aPlotter->SetDash( cursorWidth, LINE_STYLE::SOLID );
    const std::vector<CURSOR> cursors = GetCursors( aSymbol );

    for( const CURSOR& cursor : cursors )
    {
        int cursorX = 0;

        if( !CursorXToPlot( cursor, viewport, layout.plotBox, cursorX ) )
            continue;

        aPlotter->ThickSegment( VECTOR2I( cursorX, layout.plotBox.GetY() ),
                                VECTOR2I( cursorX, layout.plotBox.GetEnd().y ),
                                cursorWidth, nullptr );

        VECTOR2I point;

        if( !CursorToPlot( cursor, viewport, layout.plotBox, point ) )
            continue;

        aPlotter->ThickSegment( VECTOR2I( layout.plotBox.GetX(), point.y ),
                                VECTOR2I( layout.plotBox.GetEnd().x, point.y ),
                                cursorWidth, nullptr );
        aPlotter->Circle( point, cursorDiameter, FILL_T::FILLED_SHAPE, 0 );
    }

    const CURSOR_MEASUREMENT measurement = GetCursorMeasurement( aSymbol );

    if( measurement.valid && cursors.size() == 2 )
    {
        int x1 = 0;
        int x2 = 0;

        if( CursorXToPlot( cursors[0], viewport, layout.plotBox, x1 )
            && CursorXToPlot( cursors[1], viewport, layout.plotBox, x2 ) )
        {
            if( x1 > x2 )
                std::swap( x1, x2 );

            const int arrowY = KiROUND( layout.plotBox.GetY()
                                        + measurement.arrowY * layout.plotBox.GetHeight() );
            const int span = x2 - x1;
            const int arrowLength = std::max( 1, std::min( layout.textSize, span / 3 ) );
            const int arrowHalfHeight = std::max( 1, KiROUND( arrowLength * 0.260284 ) );
            const int arrowWidth = cursorWidth * 2;

            auto plotCursorLine = [&]( const VECTOR2I& aStart, const VECTOR2I& aEnd )
            {
                aPlotter->ThickSegment( aStart, aEnd, arrowWidth, nullptr );
            };

            if( measurement.arrowVisible )
            {
                if( span > arrowLength * 2 )
                {
                    plotCursorLine( VECTOR2I( x1 + arrowLength, arrowY ),
                                    VECTOR2I( x2 - arrowLength, arrowY ) );
                }

                const std::vector<VECTOR2I> leftArrow = {
                    VECTOR2I( x1, arrowY ),
                    VECTOR2I( x1 + arrowLength, arrowY - arrowHalfHeight ),
                    VECTOR2I( x1 + arrowLength, arrowY + arrowHalfHeight )
                };
                const std::vector<VECTOR2I> rightArrow = {
                    VECTOR2I( x2, arrowY ),
                    VECTOR2I( x2 - arrowLength, arrowY - arrowHalfHeight ),
                    VECTOR2I( x2 - arrowLength, arrowY + arrowHalfHeight )
                };
                aPlotter->PlotPoly( leftArrow, FILL_T::FILLED_SHAPE, 0, nullptr );
                aPlotter->PlotPoly( rightArrow, FILL_T::FILLED_SHAPE, 0, nullptr );
            }

            const int cursorTextSize = std::max( 1, layout.textSize * 4 / 5 );
            textAttrs.m_Size = VECTOR2I( cursorTextSize, cursorTextSize );
            textAttrs.m_StrokeWidth = std::max( 1, cursorTextSize / 10 );

            const VECTOR2I frequencyExtent = font->StringBoundaryLimits(
                    measurement.frequencyLabel, textAttrs.m_Size, textAttrs.m_StrokeWidth,
                    false, false, KIFONT::METRICS::Default() );
            const VECTOR2I periodExtent = font->StringBoundaryLimits(
                    measurement.periodLabel, textAttrs.m_Size, textAttrs.m_StrokeWidth,
                    false, false, KIFONT::METRICS::Default() );
            const int textWidth = std::max( frequencyExtent.x, periodExtent.x );
            const int textGap = std::max( cursorTextSize * 2 / 3, arrowWidth * 2 );
            const int leftSpace = x1 - layout.plotBox.GetX();
            const int rightSpace = layout.plotBox.GetEnd().x - x2;
            int textX = ( x1 + x2 ) / 2;
            GR_TEXT_H_ALIGN_T textAlign = GR_TEXT_H_ALIGN_CENTER;

            if( span < textWidth + textGap * 2 )
            {
                if( rightSpace >= textWidth + textGap )
                {
                    textX = x2 + textGap + textWidth;
                    textAlign = GR_TEXT_H_ALIGN_RIGHT;
                }
                else if( leftSpace >= textWidth + textGap )
                {
                    textX = x1 - textGap - textWidth;
                    textAlign = GR_TEXT_H_ALIGN_LEFT;
                }
                else if( rightSpace >= leftSpace )
                {
                    textX = layout.plotBox.GetEnd().x;
                    textAlign = GR_TEXT_H_ALIGN_RIGHT;
                }
                else
                {
                    textX = layout.plotBox.GetX();
                    textAlign = GR_TEXT_H_ALIGN_LEFT;
                }
            }

            textAttrs.m_Angle = ANGLE_0;
            textAttrs.m_Halign = textAlign;
            textAttrs.m_Valign = GR_TEXT_V_ALIGN_TOP;
            textAttrs.m_Color = KIGFX::COLOR4D( BLACK );
            aPlotter->PlotText(
                    VECTOR2I( textX, arrowY - cursorTextSize - textGap / 2 ),
                    KIGFX::COLOR4D( BLACK ), measurement.frequencyLabel, textAttrs, font );
            textAttrs.m_Valign = GR_TEXT_V_ALIGN_BOTTOM;
            aPlotter->PlotText(
                    VECTOR2I( textX, arrowY + cursorTextSize + textGap / 2 ),
                    KIGFX::COLOR4D( BLACK ), measurement.periodLabel, textAttrs, font );

            VECTOR2I point1;
            VECTOR2I point2;

            if( measurement.yDeltaVisible
                && CursorToPlot( cursors[0], viewport, layout.plotBox, point1 )
                && CursorToPlot( cursors[1], viewport, layout.plotBox, point2 ) )
            {
                int y1 = point1.y;
                int y2 = point2.y;

                if( y1 > y2 )
                    std::swap( y1, y2 );

                const int ySpan = y2 - y1;
                const int yArrowX = KiROUND( layout.plotBox.GetX()
                                             + measurement.yDeltaX
                                                       * layout.plotBox.GetWidth() );
                const int yArrowLength = std::max( 1, std::min( layout.textSize, ySpan / 3 ) );
                const int yArrowHalfWidth =
                        std::max( 1, KiROUND( yArrowLength * 0.260284 ) );
                const VECTOR2I yDeltaExtent = font->StringBoundaryLimits(
                        measurement.yDeltaLabel, textAttrs.m_Size, textAttrs.m_StrokeWidth,
                        false, false, KIFONT::METRICS::Default() );
                const int labelWidth = std::max( cursorTextSize, yDeltaExtent.x );
                const int labelHeight = std::max( cursorTextSize, yDeltaExtent.y );
                const int yMinText = layout.plotBox.GetY() + labelHeight / 2 + textGap;
                const int yMaxText = layout.plotBox.GetEnd().y - labelHeight / 2 - textGap;
                const int labelY = yMinText <= yMaxText
                                           ? std::clamp( ( y1 + y2 ) / 2, yMinText, yMaxText )
                                           : ( y1 + y2 ) / 2;
                GR_TEXT_H_ALIGN_T yDeltaAlign = GR_TEXT_H_ALIGN_CENTER;
                int labelX = yArrowX;
                const bool centeredLabel =
                        ySpan >= labelHeight + textGap * 4
                        && yArrowX - labelWidth / 2 >= layout.plotBox.GetX() + textGap
                        && yArrowX + labelWidth / 2
                                   <= layout.plotBox.GetEnd().x - textGap;

                if( !centeredLabel )
                {
                    if( yArrowX < layout.plotBox.GetCenter().x )
                    {
                        labelX = std::min( yArrowX + textGap,
                                           layout.plotBox.GetEnd().x - labelWidth );
                        yDeltaAlign = GR_TEXT_H_ALIGN_LEFT;
                    }
                    else
                    {
                        labelX = std::max( yArrowX - textGap,
                                           layout.plotBox.GetX() + labelWidth );
                        yDeltaAlign = GR_TEXT_H_ALIGN_RIGHT;
                    }
                }

                if( measurement.arrowVisible )
                {
                    int lineStart = y1 + yArrowLength;
                    int lineEnd = y2 - yArrowLength;

                    if( lineStart < lineEnd )
                    {
                        if( centeredLabel )
                        {
                            const int gapStart = labelY - labelHeight / 2 - textGap / 2;
                            const int gapEnd = labelY + labelHeight / 2 + textGap / 2;

                            if( lineStart < gapStart )
                            {
                                plotCursorLine( VECTOR2I( yArrowX, lineStart ),
                                                VECTOR2I( yArrowX, gapStart ) );
                            }

                            if( gapEnd < lineEnd )
                            {
                                plotCursorLine( VECTOR2I( yArrowX, gapEnd ),
                                                VECTOR2I( yArrowX, lineEnd ) );
                            }
                        }
                        else
                        {
                            plotCursorLine( VECTOR2I( yArrowX, lineStart ),
                                            VECTOR2I( yArrowX, lineEnd ) );
                        }
                    }

                    const std::vector<VECTOR2I> topArrow = {
                        VECTOR2I( yArrowX, y1 ),
                        VECTOR2I( yArrowX - yArrowHalfWidth, y1 + yArrowLength ),
                        VECTOR2I( yArrowX + yArrowHalfWidth, y1 + yArrowLength )
                    };
                    const std::vector<VECTOR2I> bottomArrow = {
                        VECTOR2I( yArrowX, y2 ),
                        VECTOR2I( yArrowX - yArrowHalfWidth, y2 - yArrowLength ),
                        VECTOR2I( yArrowX + yArrowHalfWidth, y2 - yArrowLength )
                    };
                    aPlotter->PlotPoly( topArrow, FILL_T::FILLED_SHAPE, 0, nullptr );
                    aPlotter->PlotPoly( bottomArrow, FILL_T::FILLED_SHAPE, 0, nullptr );
                }

                textAttrs.m_Angle = ANGLE_0;
                textAttrs.m_Halign = yDeltaAlign;
                textAttrs.m_Valign = GR_TEXT_V_ALIGN_CENTER;
                textAttrs.m_Color = KIGFX::COLOR4D( BLACK );
                aPlotter->PlotText( VECTOR2I( labelX, labelY ), KIGFX::COLOR4D( BLACK ),
                                    measurement.yDeltaLabel, textAttrs, font );
            }
        }
    }

    if( measurement.singleCursorValid && cursors.size() == 1 )
    {
        int cursorX = 0;
        VECTOR2I point;
        const int cursorTextSize = std::max( 1, layout.textSize * 4 / 5 );
        const int textGap = std::max( cursorTextSize * 2 / 3, cursorWidth * 4 );
        textAttrs.m_Size = VECTOR2I( cursorTextSize, cursorTextSize );
        textAttrs.m_StrokeWidth = std::max( 1, cursorTextSize / 10 );
        textAttrs.m_Angle = ANGLE_0;
        textAttrs.m_Color = KIGFX::COLOR4D( BLACK );

        if( CursorXToPlot( cursors.front(), viewport, layout.plotBox, cursorX ) )
        {
            const VECTOR2I xExtent = font->StringBoundaryLimits(
                    measurement.cursorXLabel, textAttrs.m_Size, textAttrs.m_StrokeWidth,
                    false, false, KIFONT::METRICS::Default() );
            const int labelWidth = std::max( cursorTextSize, xExtent.x );
            const int labelHeight = std::max( cursorTextSize, xExtent.y );
            int labelX = cursorX + textGap;
            GR_TEXT_H_ALIGN_T xAlign = GR_TEXT_H_ALIGN_LEFT;

            if( labelX + labelWidth > layout.plotBox.GetEnd().x )
            {
                labelX = cursorX - textGap;
                xAlign = GR_TEXT_H_ALIGN_RIGHT;
            }

            if( labelX - labelWidth < layout.plotBox.GetX() )
            {
                labelX = layout.plotBox.GetEnd().x - textGap;
                xAlign = GR_TEXT_H_ALIGN_RIGHT;
            }

            const int labelY = std::min( layout.plotBox.GetY() + textGap,
                                         layout.plotBox.GetEnd().y - labelHeight );
            textAttrs.m_Halign = xAlign;
            textAttrs.m_Valign = GR_TEXT_V_ALIGN_TOP;
            aPlotter->PlotText( VECTOR2I( labelX, labelY ), KIGFX::COLOR4D( BLACK ),
                                measurement.cursorXLabel, textAttrs, font );
        }

        if( CursorToPlot( cursors.front(), viewport, layout.plotBox, point ) )
        {
            const VECTOR2I yExtent = font->StringBoundaryLimits(
                    measurement.cursorYLabel, textAttrs.m_Size, textAttrs.m_StrokeWidth,
                    false, false, KIFONT::METRICS::Default() );
            const int labelWidth = std::max( cursorTextSize, yExtent.x );
            const int labelHeight = std::max( cursorTextSize, yExtent.y );
            int labelX = layout.plotBox.GetX() + textGap;
            GR_TEXT_H_ALIGN_T yAlign = GR_TEXT_H_ALIGN_LEFT;

            if( labelX + labelWidth > layout.plotBox.GetEnd().x )
            {
                labelX = layout.plotBox.GetEnd().x - textGap;
                yAlign = GR_TEXT_H_ALIGN_RIGHT;
            }

            GR_TEXT_V_ALIGN_T yValign = GR_TEXT_V_ALIGN_BOTTOM;
            int labelY = point.y - textGap;

            if( labelY - labelHeight < layout.plotBox.GetY() )
            {
                labelY = point.y + textGap;
                yValign = GR_TEXT_V_ALIGN_TOP;
            }

            if( labelY + labelHeight > layout.plotBox.GetEnd().y )
            {
                labelY = point.y - textGap;
                yValign = GR_TEXT_V_ALIGN_BOTTOM;
            }

            textAttrs.m_Halign = yAlign;
            textAttrs.m_Valign = yValign;
            aPlotter->PlotText( VECTOR2I( labelX, labelY ), KIGFX::COLOR4D( BLACK ),
                                measurement.cursorYLabel, textAttrs, font );
        }

        textAttrs.m_Size = VECTOR2I( layout.textSize, layout.textSize );
        textAttrs.m_StrokeWidth = std::max( 1, layout.textSize / 10 );
    }

    aPlotter->SetColor( settings.borderColor );
    aPlotter->SetDash( settings.gridWidth, LINE_STYLE::SOLID );
    aPlotter->ThickRect( layout.plotBox.GetOrigin(), layout.plotBox.GetEnd(),
                         std::max( 1, settings.gridWidth ), nullptr );

    aPlotter->SetDash( settings.gridWidth, LINE_STYLE::SOLID );
}


SCH_SCOPE::VIEWPORT SCH_SCOPE::GetViewport( const SCH_SYMBOL* aSymbol )
{
    if( !aSymbol )
        return {};

    auto it = s_scopeRuntime.find( aSymbol->m_Uuid );
    return it == s_scopeRuntime.end() ? VIEWPORT() : it->second.viewport;
}


bool SCH_SCOPE::ZoomViewport( const SCH_SYMBOL* aSymbol, const VECTOR2D& aAnchor,
                              double aFactor )
{
    if( !aSymbol || !std::isfinite( aFactor ) || aFactor <= 0.0 )
        return false;

    VIEWPORT& viewport = s_scopeRuntime[aSymbol->m_Uuid].viewport;
    VIEWPORT  previous = viewport;
    const double anchorX = std::clamp( aAnchor.x, 0.0, 1.0 );
    const double anchorY = std::clamp( aAnchor.y, 0.0, 1.0 );
    const double xRange = viewport.xMax - viewport.xMin;
    const double yRange = viewport.yMax - viewport.yMin;
    const double xAtAnchor = viewport.xMin + anchorX * xRange;
    const double yAtAnchor = viewport.yMin + anchorY * yRange;
    const double newXRange = std::clamp( xRange / aFactor, 1e-5, 1.0 );
    const double newYRange = std::clamp( yRange / aFactor, 1e-5, 1.0 );

    viewport.xMin = xAtAnchor - anchorX * newXRange;
    viewport.xMax = viewport.xMin + newXRange;
    viewport.yMin = yAtAnchor - anchorY * newYRange;
    viewport.yMax = viewport.yMin + newYRange;
    clampViewportAxis( viewport.xMin, viewport.xMax );
    clampViewportAxis( viewport.yMin, viewport.yMax );

    const bool changed = std::abs( viewport.xMin - previous.xMin ) > 1e-9
                         || std::abs( viewport.xMax - previous.xMax ) > 1e-9
                         || std::abs( viewport.yMin - previous.yMin ) > 1e-9
                         || std::abs( viewport.yMax - previous.yMax ) > 1e-9;

    if( changed )
    {
        SCOPE_RUNTIME_STATE& runtime = s_scopeRuntime[aSymbol->m_Uuid];
        updateCursorMeasurement( aSymbol, GetSettings( aSymbol ), runtime );
    }

    return changed;
}


bool SCH_SCOPE::PanViewport( const SCH_SYMBOL* aSymbol, const VECTOR2D& aDelta )
{
    if( !aSymbol )
        return false;

    VIEWPORT& viewport = s_scopeRuntime[aSymbol->m_Uuid].viewport;
    VIEWPORT  previous = viewport;
    const double xOffset = aDelta.x * ( viewport.xMax - viewport.xMin );
    const double yOffset = aDelta.y * ( viewport.yMax - viewport.yMin );

    viewport.xMin += xOffset;
    viewport.xMax += xOffset;
    viewport.yMin += yOffset;
    viewport.yMax += yOffset;
    clampViewportAxis( viewport.xMin, viewport.xMax );
    clampViewportAxis( viewport.yMin, viewport.yMax );

    const bool changed = std::abs( viewport.xMin - previous.xMin ) > 1e-9
                         || std::abs( viewport.xMax - previous.xMax ) > 1e-9
                         || std::abs( viewport.yMin - previous.yMin ) > 1e-9
                         || std::abs( viewport.yMax - previous.yMax ) > 1e-9;

    if( changed )
    {
        SCOPE_RUNTIME_STATE& runtime = s_scopeRuntime[aSymbol->m_Uuid];
        updateCursorMeasurement( aSymbol, GetSettings( aSymbol ), runtime );
    }

    return changed;
}


void SCH_SCOPE::ResetViewport( const SCH_SYMBOL* aSymbol )
{
    if( aSymbol )
    {
        SCOPE_RUNTIME_STATE& runtime = s_scopeRuntime[aSymbol->m_Uuid];
        runtime.viewport = VIEWPORT();
        updateCursorMeasurement( aSymbol, GetSettings( aSymbol ), runtime );
    }
}


SCH_SCOPE::ZOOM_SELECTION SCH_SCOPE::GetZoomSelection( const SCH_SYMBOL* aSymbol )
{
    if( !aSymbol )
        return {};

    auto it = s_scopeRuntime.find( aSymbol->m_Uuid );
    return it == s_scopeRuntime.end() ? ZOOM_SELECTION() : it->second.zoomSelection;
}


void SCH_SCOPE::BeginZoomSelection( const SCH_SYMBOL* aSymbol, const VECTOR2D& aPosition )
{
    if( !aSymbol )
        return;

    ZOOM_SELECTION& selection = s_scopeRuntime[aSymbol->m_Uuid].zoomSelection;
    selection.active = true;
    selection.start.x = std::clamp( aPosition.x, 0.0, 1.0 );
    selection.start.y = std::clamp( aPosition.y, 0.0, 1.0 );
    selection.end = selection.start;
}


bool SCH_SCOPE::UpdateZoomSelection( const SCH_SYMBOL* aSymbol, const VECTOR2D& aPosition )
{
    if( !aSymbol )
        return false;

    ZOOM_SELECTION& selection = s_scopeRuntime[aSymbol->m_Uuid].zoomSelection;

    if( !selection.active )
        return false;

    const VECTOR2D next( std::clamp( aPosition.x, 0.0, 1.0 ),
                         std::clamp( aPosition.y, 0.0, 1.0 ) );

    if( std::abs( next.x - selection.end.x ) < 1e-9
        && std::abs( next.y - selection.end.y ) < 1e-9 )
    {
        return false;
    }

    selection.end = next;
    return true;
}


bool SCH_SCOPE::FinishZoomSelection( const SCH_SYMBOL* aSymbol )
{
    if( !aSymbol )
        return false;

    SCOPE_RUNTIME_STATE& runtime = s_scopeRuntime[aSymbol->m_Uuid];
    ZOOM_SELECTION& selection = runtime.zoomSelection;

    if( !selection.active )
        return false;

    selection.active = false;
    const double left = std::min( selection.start.x, selection.end.x );
    const double right = std::max( selection.start.x, selection.end.x );
    const double bottom = std::min( selection.start.y, selection.end.y );
    const double top = std::max( selection.start.y, selection.end.y );

    if( right - left < 0.01 || top - bottom < 0.01 )
        return false;

    const VIEWPORT previous = runtime.viewport;
    const double xRange = previous.xMax - previous.xMin;
    const double yRange = previous.yMax - previous.yMin;
    runtime.viewport.xMin = previous.xMin + left * xRange;
    runtime.viewport.xMax = previous.xMin + right * xRange;
    runtime.viewport.yMin = previous.yMin + bottom * yRange;
    runtime.viewport.yMax = previous.yMin + top * yRange;
    clampViewportAxis( runtime.viewport.xMin, runtime.viewport.xMax );
    clampViewportAxis( runtime.viewport.yMin, runtime.viewport.yMax );
    updateCursorMeasurement( aSymbol, GetSettings( aSymbol ), runtime );
    return true;
}


void SCH_SCOPE::CancelZoomSelection( const SCH_SYMBOL* aSymbol )
{
    if( aSymbol )
        s_scopeRuntime[aSymbol->m_Uuid].zoomSelection = ZOOM_SELECTION();
}


wxString SCH_SCOPE::GetClass() const
{
    return wxT( "SCH_SCOPE" );
}


wxString SCH_SCOPE::GetFriendlyName() const
{
    return _( "Scope" );
}


wxString SCH_SCOPE::GetItemDescription( UNITS_PROVIDER* aUnitsProvider, bool aFull ) const
{
    (void) aFull;

    return wxString::Format( _( "Scope, width %s height %s" ),
                             aUnitsProvider->MessageTextFromValue( std::abs( GetStart().x - GetEnd().x ) ),
                             aUnitsProvider->MessageTextFromValue( std::abs( GetStart().y - GetEnd().y ) ) );
}


BITMAPS SCH_SCOPE::GetMenuImage() const
{
    return BITMAPS::add_scope;
}


EDA_ITEM* SCH_SCOPE::Clone() const
{
    return new SCH_SCOPE( *this );
}
