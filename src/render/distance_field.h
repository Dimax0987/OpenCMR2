#pragma once
#include <vector>

namespace DistanceField {
// Positive inside, negative outside; coverage is in [0, 1].
std::vector<float> FromCoverage(const std::vector<float> &coverage, int width, int height);
} // namespace DistanceField
