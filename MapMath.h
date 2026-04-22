#pragma once
#include <cmath>

namespace MapMath {
    constexpr double PI = 3.141592653589793238462643383279502884;
    constexpr double RAD = PI / 180.0;
    constexpr double DEG = 180.0 / PI;

    inline double lon2x(double lon, int z) {
        return (lon + 180.0) / 360.0 * (1 << z);
    }

    inline double lat2y(double lat, int z) {
        return (1.0 - std::asinh(std::tan(lat * RAD)) / PI) / 2.0 * (1 << z);
    }

    inline double x2lon(double x, int z) {
        return x / (1 << z) * 360.0 - 180.0;
    }

    inline double y2lat(double y, int z) {
        const double n = PI - 2.0 * PI * y / (1 << z);
        return DEG * std::atan(0.5 * (std::exp(n) - std::exp(-n)));
    }
}