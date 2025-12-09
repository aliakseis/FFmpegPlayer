#pragma once

#include "ordered_scoped_token.hpp"

#include <cstdint>
#include <vector>

bool CanUpscaleImage();

bool EnableImageUpscale();

void ImageUpscale(OrderedScopedTokenGenerator::Token, uint8_t* input, int inputStride, int inputWidth, int inputHeight,
    std::vector<uint8_t>& output, int& outputWidth, int& outputHeight);
