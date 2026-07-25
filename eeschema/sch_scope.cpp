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
#include <lib_symbol.h>
#include <sch_pin.h>
#include <sch_symbol.h>
#include <sch_text.h>
#include <units_provider.h>

#include <algorithm>
#include <cstdlib>
#include <vector>


namespace
{
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
} // namespace


SCH_SCOPE::SCH_SCOPE( const VECTOR2I& aPosition, SCH_LAYER_ID aLayer, int aLineWidth,
                      FILL_T aFillType ) :
        SCH_SHAPE( SHAPE_T::RECTANGLE, aLayer, aLineWidth, aFillType, SCH_SHAPE_T )
{
    SetStart( aPosition );
    SetEnd( aPosition + DefaultSize() );
}


VECTOR2I SCH_SCOPE::DefaultSize()
{
    return VECTOR2I( schIUScale.MilsToIU( 800 ), schIUScale.MilsToIU( 500 ) );
}


VECTOR2I SCH_SCOPE::MinimumSize()
{
    return MinimumSize( 1 );
}


VECTOR2I SCH_SCOPE::MinimumSize( int aChannelCount )
{
    const int channelCount = std::clamp( aChannelCount, 1, MaxChannelCount() );
    const int height = channelMargin() + channelStackHeight( channelCount ) + channelMargin();
    const int width = PinPitch() + GridSize();

    return VECTOR2I( width, height );
}


LIB_ID SCH_SCOPE::LibId()
{
    return LIB_ID( wxS( "Simulation" ), wxS( "Scope" ) );
}


bool SCH_SCOPE::IsScopeSymbol( const SCH_SYMBOL* aSymbol )
{
    return aSymbol && aSymbol->GetLibId() == LibId();
}


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


int SCH_SCOPE::GridSize()
{
    return schIUScale.MilsToIU( 50 );
}


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
