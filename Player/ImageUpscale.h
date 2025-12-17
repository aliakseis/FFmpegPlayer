#pragma once

#include "ordered_scoped_token.h"

#include <cstdint>
#include <vector>

bool CanUpscaleImage();

bool EnableImageUpscale();

bool ImageUpscale(OrderedScopedTokenGenerator::Token, uint8_t* input, int inputStride, int inputWidth, int inputHeight,
    int64_t, std::vector<uint8_t>& output, int& outputWidth, int& outputHeight);
