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


class LIB_SYMBOL;
class SCH_SYMBOL;


class SCH_SCOPE : public SCH_SHAPE
{
public:
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

    SCH_SCOPE( const VECTOR2I& aPosition = VECTOR2I( 0, 0 ), SCH_LAYER_ID aLayer = LAYER_DEVICE,
               int aLineWidth = 0, FILL_T aFillType = FILL_T::FILLED_WITH_BG_BODYCOLOR );

    static VECTOR2I DefaultSize();
    static VECTOR2I MinimumSize();
    static VECTOR2I MinimumSize( int aChannelCount );
    static LIB_ID LibId();
    static bool IsScopeSymbol( const SCH_SYMBOL* aSymbol );
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
    static int GridSize();
    static int PinTextSize();
    static int PinNameOffset();
    static int ChannelModeTextSize();
    static int ChannelModeButtonSize();

    wxString GetClass() const override;

    wxString GetFriendlyName() const override;

    wxString GetItemDescription( UNITS_PROVIDER* aUnitsProvider, bool aFull ) const override;

    BITMAPS GetMenuImage() const override;

    EDA_ITEM* Clone() const override;
};


#endif /* SCH_SCOPE_H */
