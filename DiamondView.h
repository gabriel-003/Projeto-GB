#ifndef DiamondView_h
#define DiamondView_h
#include "TilemapView.h"

class DiamondView : public TilemapView {
public:
    void computeDrawPosition(const int col, const int row, const float tw, const float th, float &x, float &y) const override {
        x = col * tw/2.0f + row * tw/2.0f;
        y = col * th/2.0f - row * th/2.0f;
    }

    void computeMouseMap(int &col, int &row, const float tw, const float th, const float mx, const float my) const override {
        col = (int)((mx / (tw/2.0f) + my / (th/2.0f)) / 2.0f);
        row = (int)((mx / (tw/2.0f) - my / (th/2.0f)) / 2.0f);
    }

    void computeTileWalking(int &col, int &row, const int direction) const override {
        switch(direction) {
            case DIRECTION_NORTH:     row++; break;
            case DIRECTION_SOUTH:     row--; break;
            case DIRECTION_EAST:      col++; break;
            case DIRECTION_WEST:      col--; break;
            case DIRECTION_NORTHEAST: col++; row++; break;
            case DIRECTION_NORTHWEST: col--; row++; break;
            case DIRECTION_SOUTHEAST: col++; row--; break;
            case DIRECTION_SOUTHWEST: col--; row--; break;
        }
    }
};
#endif
