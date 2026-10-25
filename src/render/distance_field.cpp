// Euclidean distance transform shared by fonts and vector sprites.
#include "distance_field.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace DistanceField {
// Squared Euclidean distance transform (Felzenszwalb and Huttenlocher).
static void DistanceTransform1D(const float *f, float *d, int n, std::vector<int> &v, std::vector<double> &z) {
    int k = 0;
    v[0] = 0;
    z[0] = -1e30;
    z[1] = 1e30;
    for (int q = 1; q < n; q++) {
        double s;
        for (;;) {
            int p = v[k];
            s = (((double)f[q] + (double)q * q) - ((double)f[p] + (double)p * p)) / (2.0 * (q - p));
            if (s > z[k])
                break;
            k--;
        }
        k++;
        v[k] = q;
        z[k] = s;
        z[k + 1] = 1e30;
    }
    k = 0;
    for (int q = 0; q < n; q++) {
        while (z[k + 1] < q)
            k++;
        double dq = q - v[k];
        d[q] = (float)std::min(dq * dq + f[v[k]], 1e20);
    }
}

static void DistanceTransform(std::vector<float> &grid, int w, int h) {
    const int n = std::max(w, h);
    std::vector<float> f(n), d(n);
    std::vector<double> z(n + 1);
    std::vector<int> v(n);
    for (int x = 0; x < w; x++) {
        for (int y = 0; y < h; y++)
            f[y] = grid[(size_t)y * w + x];
        DistanceTransform1D(f.data(), d.data(), h, v, z);
        for (int y = 0; y < h; y++)
            grid[(size_t)y * w + x] = d[y];
    }
    for (int y = 0; y < h; y++) {
        DistanceTransform1D(&grid[(size_t)y * w], d.data(), w, v, z);
        memcpy(&grid[(size_t)y * w], d.data(), w * sizeof(float));
    }
}

// Signed distances of a coverage's samples to its edge (at 0.5), in
// samples, positive inside.
std::vector<float> FromCoverage(const std::vector<float> &coverage, int w, int h) {
    const float inf = 1e20f;
    std::vector<float> toInside(coverage.size()), toOutside(coverage.size());
    for (size_t i = 0; i < coverage.size(); i++) {
        bool in = coverage[i] >= 0.5f;
        toInside[i] = in ? 0.0f : inf;
        toOutside[i] = in ? inf : 0.0f;
    }
    DistanceTransform(toInside, w, h);
    DistanceTransform(toOutside, w, h);
    for (size_t i = 0; i < coverage.size(); i++)
        toInside[i] = toOutside[i] > 0 ? sqrtf(toOutside[i]) - 0.5f : 0.5f - sqrtf(toInside[i]);
    return toInside;
}

} // namespace DistanceField
