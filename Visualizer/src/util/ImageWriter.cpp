#include "ImageWriter.h"
#include "fpng.h"

#include <stdexcept>

void saveColorImageToPng(const std::vector<Color>& pixels, int width, int height, const std::string& filename) {
    std::vector<unsigned char> rgb_data(width * height * 3);

    for (int y = 0; y < height; ++y) {
        const int target_y = height - 1 - y;
        for (int x = 0; x < width; ++x) {
            const Color& p = pixels[y * width + x];
            const int di = (target_y * width + x) * 3;
            rgb_data[di + 0] = p.red;
            rgb_data[di + 1] = p.green;
            rgb_data[di + 2] = p.blue;
        }
    }

    if (!fpng::fpng_encode_image_to_file(filename.c_str(), rgb_data.data(), width, height, 3))
        throw std::runtime_error("Failed to write PNG file: " + filename);
}
