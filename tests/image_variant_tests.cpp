#include "homecloud/image_variant.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

using namespace std;

namespace {

void write_u32(vector<uint8_t>& bytes, size_t offset, uint32_t value) {
    for (size_t index = 0; index < 4; ++index)
        bytes[offset + index] = static_cast<uint8_t>(value >> (index * 8));
}

void write_u16(vector<uint8_t>& bytes, size_t offset, uint16_t value) {
    bytes[offset] = static_cast<uint8_t>(value);
    bytes[offset + 1] = static_cast<uint8_t>(value >> 8);
}

filesystem::path create_test_bitmap() {
    constexpr uint32_t width = 640;
    constexpr uint32_t height = 360;
    constexpr uint32_t row_size = width * 3;
    vector<uint8_t> bytes(54 + row_size * height, 0);
    bytes[0] = 'B'; bytes[1] = 'M';
    write_u32(bytes, 2, static_cast<uint32_t>(bytes.size()));
    write_u32(bytes, 10, 54); write_u32(bytes, 14, 40);
    write_u32(bytes, 18, width); write_u32(bytes, 22, height);
    write_u16(bytes, 26, 1); write_u16(bytes, 28, 24);
    write_u32(bytes, 34, row_size * height);
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const size_t pixel = 54 + y * row_size + x * 3;
            bytes[pixel] = static_cast<uint8_t>(x % 256);
            bytes[pixel + 1] = static_cast<uint8_t>(y % 256);
            bytes[pixel + 2] = 90;
        }
    }
    const auto path = filesystem::temp_directory_path() / "homecloud-image-variant-test.bmp";
    ofstream output(path, ios::binary | ios::trunc);
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<streamsize>(bytes.size()));
    return path;
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc == 2) {
        const filesystem::path source = argv[1];
        const auto thumbnail = homecloud::create_jpeg_variant(source, 320, 320, 0.74F);
        const auto preview = homecloud::create_jpeg_variant(source, 1920, 1920, 0.86F);
        if (!thumbnail || !preview) return 1;
        cout << "original=" << filesystem::file_size(source)
             << " thumbnail=" << thumbnail->bytes.size()
             << " preview=" << preview->bytes.size() << '\n';
        return 0;
    }
    const auto source = create_test_bitmap();
    const auto variant = homecloud::create_jpeg_variant(source, 320, 320, 0.75F);
    error_code ignored;
    filesystem::remove(source, ignored);
    if (!variant || variant->bytes.size() < 4 || variant->width != 320 || variant->height != 180 ||
        variant->bytes[0] != 0xff || variant->bytes[1] != 0xd8) {
        cerr << "Image variant generation failed\n";
        return 1;
    }
    cout << "Image variant test passed: " << variant->bytes.size() << " bytes\n";
    return 0;
}
