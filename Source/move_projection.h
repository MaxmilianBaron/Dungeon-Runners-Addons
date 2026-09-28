#pragma once
#include <array>
#include <cmath>

struct MoveProjection {
    float sx = 1, sy = 1, tx = 0, ty = 0;
    static MoveProjection Around(float x,float y,float sx,float sy,float width,float height) {
        return {sx,sy,(sx-1)*(1-2*x/width),(1-sy)*(1-2*y/height)};
    }
    std::array<float,16> Apply(const std::array<float,16>& matrix) const {
        auto result = matrix;
        for (unsigned row = 0; row < 4; ++row) {
            result[row*4] = matrix[row*4]*sx + matrix[row*4+3]*tx;
            result[row*4+1] = matrix[row*4+1]*sy + matrix[row*4+3]*ty;
        }
        return result;
    }
};
