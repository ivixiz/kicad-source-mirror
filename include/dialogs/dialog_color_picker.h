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

#pragma once

#include <dialog_shim.h>
#include <gal/color4d.h>

#include <memory>
#include <vector>


class COLOR_SWATCH;


/** A named predefined color shown in the color picker's compact swatch bar. */
struct CUSTOM_COLOR_ITEM
{
    KIGFX::COLOR4D m_Color;
    wxString       m_ColorName;

    CUSTOM_COLOR_ITEM( double aRed, double aGreen, double aBlue, const wxString& aName ) :
            m_Color( aRed, aGreen, aBlue, 1.0 ),
            m_ColorName( aName )
    {}

    CUSTOM_COLOR_ITEM( double aRed, double aGreen, double aBlue, double aAlpha,
                       const wxString& aName ) :
            m_Color( aRed, aGreen, aBlue, aAlpha ),
            m_ColorName( aName )
    {}

    CUSTOM_COLOR_ITEM( const KIGFX::COLOR4D& aColor, const wxString& aName ) :
            m_Color( aColor ),
            m_ColorName( aName )
    {}
};


/**
 * Compact native color picker used by KiCad color swatches.
 *
 * The public API intentionally matches the legacy generated dialog so callers do not need to
 * know which implementation is in use.  The controls and rendering implementation are kept in
 * the source file to make this component self-contained and inexpensive to construct.
 */
class DIALOG_COLOR_PICKER : public DIALOG_SHIM
{
public:
    /**
     * @param aParent parent window.
     * @param aCurrentColor color selected when the dialog opens.
     * @param aAllowOpacityControl true to edit alpha, false to force concrete colors opaque.
     * @param aUserColors optional named colors for the compact swatch bar.
     * @param aDefaultColor value selected by Reset to Default/Clear Color.
     */
    DIALOG_COLOR_PICKER( wxWindow* aParent, const KIGFX::COLOR4D& aCurrentColor,
                         bool aAllowOpacityControl,
                         std::vector<CUSTOM_COLOR_ITEM>* aUserColors = nullptr,
                         const KIGFX::COLOR4D& aDefaultColor = KIGFX::COLOR4D::UNSPECIFIED );

    ~DIALOG_COLOR_PICKER() override;

    KIGFX::COLOR4D GetColor() const;

    bool Show( bool aShow = true ) override;

protected:
    void OnCharHook( wxKeyEvent& aEvent ) override;

private:
    struct IMPL;
    std::unique_ptr<IMPL> m_impl;
};
