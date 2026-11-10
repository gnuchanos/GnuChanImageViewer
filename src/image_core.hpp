#pragma once

#include <gdk-pixbuf/gdk-pixbuf.h>

#include <filesystem>
#include <cstdint>
#include <string>
#include <vector>

namespace image {

struct Metadata {
    int width = 0;
    int height = 0;
    std::uintmax_t file_size = 0;
    std::string format;
    std::string exif;
    std::string color_profile;
    bool has_alpha = false;
    bool animated = false;
};

struct CropRect {
    int x;
    int y;
    int width;
    int height;
};

std::vector<std::string> available_extensions();
std::vector<std::string> available_formats();
bool can_decode(const std::filesystem::path& path);
std::string encoder_for_extension(const std::filesystem::path& path);
bool save_webp(const std::filesystem::path& path, GdkPixbuf* pixbuf, std::string& error);
std::vector<std::filesystem::path> images_in_directory(const std::filesystem::path& directory);
Metadata read_metadata(const std::filesystem::path& path, GdkPixbuf* pixbuf, bool animated);
GdkPixbuf* rotate(GdkPixbuf* pixbuf, int degrees);
GdkPixbuf* flip(GdkPixbuf* pixbuf, bool horizontal);
GdkPixbuf* crop(GdkPixbuf* pixbuf, CropRect rect);
GdkPixbuf* load_pixbuf(const std::filesystem::path& path, GError** error);
std::string supported_format_list();

}  // namespace image
