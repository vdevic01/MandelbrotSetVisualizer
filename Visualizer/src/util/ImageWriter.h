#pragma once

#include <string>
#include <vector>
#include "ColorManager.h"

void saveColorImageToPng(const std::vector<Color>& pixels, int width, int height, const std::string& filename);
