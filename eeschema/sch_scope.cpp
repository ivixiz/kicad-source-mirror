//FILE: sch_scope.cpp
#include "sch_scope.h"

SCH_SCOPE::SCH_SCOPE( const VECTOR2I& aPosition, SCH_LAYER_ID aLayer ) :
    SCH_ITEM( nullptr,  SCH_SCOPE_T ){
    m_pos   = aPosition;
    m_layer = aLayer;

}
EDA_ITEM* SCH_SCOPE::Clone() const                { return new SCH_SCOPE(*this); }
void SCH_SCOPE::MirrorVertically( int aCenter )   { MIRROR( m_pos.y, aCenter );  }
void SCH_SCOPE::MirrorHorizontally( int aCenter ) { MIRROR( m_pos.x, aCenter );  }
void SCH_SCOPE::Rotate( const VECTOR2I& aCenter, bool aRotateCCW ){
    RotatePoint( m_pos, aCenter, aRotateCCW ? ANGLE_90 : ANGLE_270 );
}
std::vector<VECTOR2I> SCH_SCOPE::GetConnectionPoints() const{
    return { m_pos };
}

void SCH_SCOPE::Plot( PLOTTER* aPlotter, bool aBackground, const SCH_PLOT_OPTS& aPlotOpts,
                         int aUnit, int aBodyStyle, const VECTOR2I& aOffset, bool aDimmed )
{
    if( aBackground )
        return;

    RENDER_SETTINGS* settings = aPlotter->RenderSettings();
    COLOR4D          color = COLOR4D(0.80, 0.80, 0.90, 1.0 );

    if( color == COLOR4D::UNSPECIFIED )
        color = settings->GetLayerColor( GetLayer() );

    aPlotter->SetColor( color );

    aPlotter->Circle( m_pos, KiROUND( schIUScale.MilsToIU( DEFAULT_WIRE_WIDTH_MILS ) * 1.7 ), FILL_T::FILLED_SHAPE );
}