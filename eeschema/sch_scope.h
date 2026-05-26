//FILE: sch_scope.h
#pragma once

#include <sch_shape.h>

/**
 * Object to handle a scope that can be inserted in a schematic.
 */
class SCH_SCOPE : public SCH_ITEM
{
public:
    SCH_SCOPE( const VECTOR2I& aPosition = VECTOR2I( 0, 0 ), SCH_LAYER_ID aLayer = LAYER_DEVICE );
    ~SCH_SCOPE() { }

    wxString GetClass() const override
    {
        return wxT( "SCH_SCOPE" );
    }
    EDA_ITEM* Clone() const override;
    void SwapData( SCH_ITEM* aItem ) override;
    
    void Move( const VECTOR2I& aMoveVector ) override
    {
        m_pos += aMoveVector;
    }
    void MirrorHorizontally( int aCenter ) override;
    void MirrorVertically( int aCenter ) override;
    void Rotate( const VECTOR2I& aCenter, bool aRotateCCW ) override;
    std::vector<VECTOR2I> GetConnectionPoints() const override;
    void Plot( PLOTTER* aPlotter, bool aBackground, const SCH_PLOT_OPTS& aPlotOpts,
               int aUnit, int aBodyStyle, const VECTOR2I& aOffset, bool aDimmed ) override;

private:
    VECTOR2I m_pos;
    int      m_radius = schIUScale.MilsToIU(50);
};
