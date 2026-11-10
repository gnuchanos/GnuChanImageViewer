#include "image_core.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <iterator>
#include <sstream>
#include <webp/decode.h>
#include <webp/encode.h>

namespace fs = std::filesystem;

namespace image {
namespace {

void release_webp_pixels(guchar* pixels, gpointer) {
    WebPFree(pixels);
}

std::uint16_t read16(const std::vector<unsigned char>& bytes, std::size_t pos, bool little);
std::uint32_t read32(const std::vector<unsigned char>& bytes, std::size_t pos, bool little);

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool starts_with(const std::vector<unsigned char>& bytes, std::size_t offset,
                 const char* text, std::size_t length) {
    return offset <= bytes.size() && length <= bytes.size() - offset &&
           std::equal(text, text + length, bytes.begin() + static_cast<std::ptrdiff_t>(offset));
}

std::string exif_summary(const std::vector<unsigned char>& bytes) {
    std::size_t offset = 2;
    while (offset + 4 <= bytes.size() && bytes[offset] == 0xff) {
        const unsigned char marker = bytes[offset + 1];
        offset += 2;
        if (marker == 0xda || marker == 0xd9) {
            break;
        }
        if (marker == 0xd8 || marker == 0x01 || (marker >= 0xd0 && marker <= 0xd7)) {
            continue;
        }
        const std::size_t length = (static_cast<std::size_t>(bytes[offset]) << 8) | bytes[offset + 1];
        if (length < 2 || length > bytes.size() - offset) {
            break;
        }
        if (marker == 0xe1 && length >= 8 &&
            starts_with(bytes, offset + 2, "Exif\0\0", 6)) {
            const std::size_t tiff = offset + 8;
            if (tiff + 8 > bytes.size()) {
                break;
            }
            const bool little = bytes[tiff] == 'I' && bytes[tiff + 1] == 'I';
            const bool big = bytes[tiff] == 'M' && bytes[tiff + 1] == 'M';
            if ((!little && !big) || read16(bytes, tiff + 2, little) != 42) {
                break;
            }
            const std::uint32_t ifd_offset = read32(bytes, tiff + 4, little);
            const std::size_t ifd = tiff + ifd_offset;
            if (ifd + 2 > bytes.size()) {
                break;
            }
            std::vector<std::string> fields;
            const std::uint16_t count = read16(bytes, ifd, little);
            for (std::uint16_t i = 0; i < count; ++i) {
                const std::size_t entry = ifd + 2 + static_cast<std::size_t>(i) * 12;
                if (entry + 12 > bytes.size()) {
                    break;
                }
                const std::uint16_t tag = read16(bytes, entry, little);
                const std::uint16_t type = read16(bytes, entry + 2, little);
                const std::uint32_t n = read32(bytes, entry + 4, little);
                if ((tag == 0x010f || tag == 0x0110 || tag == 0x0132) && type == 2 && n > 0) {
                    const std::size_t value = n <= 4 ? entry + 8 : tiff + read32(bytes, entry + 8, little);
                    if (value <= bytes.size() && n <= bytes.size() - value) {
                        std::string text(bytes.begin() + static_cast<std::ptrdiff_t>(value),
                                         bytes.begin() + static_cast<std::ptrdiff_t>(value + n));
                        while (!text.empty() && (text.back() == '\0' || std::isspace(static_cast<unsigned char>(text.back())))) {
                            text.pop_back();
                        }
                        if (!text.empty()) {
                            const char* label = tag == 0x010f ? "Camera maker" :
                                                tag == 0x0110 ? "Camera model" : "Date taken";
                            fields.push_back(std::string(label) + ": " + text);
                        }
                    }
                } else if (tag == 0x0112 && type == 3 && n == 1) {
                    const std::uint16_t orientation = read16(bytes, entry + 8, little);
                    if (orientation >= 1 && orientation <= 8 && orientation != 1) {
                        fields.push_back("EXIF orientation: " + std::to_string(orientation));
                    }
                }
            }
            if (fields.empty()) {
                return "EXIF data present";
            }
            std::ostringstream result;
            for (std::size_t i = 0; i < fields.size(); ++i) {
                if (i) result << "\n";
                result << fields[i];
            }
            return result.str();
        }
        offset += length;
    }
    return "No EXIF metadata";
}

std::uint16_t read16(const std::vector<unsigned char>& bytes, std::size_t pos, bool little) {
    if (pos + 2 > bytes.size()) return 0;
    return little ? static_cast<std::uint16_t>(bytes[pos] | (bytes[pos + 1] << 8))
                  : static_cast<std::uint16_t>((bytes[pos] << 8) | bytes[pos + 1]);
}

std::uint32_t read32(const std::vector<unsigned char>& bytes, std::size_t pos, bool little) {
    if (pos + 4 > bytes.size()) return 0;
    if (little) {
        return static_cast<std::uint32_t>(bytes[pos]) |
               (static_cast<std::uint32_t>(bytes[pos + 1]) << 8) |
               (static_cast<std::uint32_t>(bytes[pos + 2]) << 16) |
               (static_cast<std::uint32_t>(bytes[pos + 3]) << 24);
    }
    return (static_cast<std::uint32_t>(bytes[pos]) << 24) |
           (static_cast<std::uint32_t>(bytes[pos + 1]) << 16) |
           (static_cast<std::uint32_t>(bytes[pos + 2]) << 8) |
           static_cast<std::uint32_t>(bytes[pos + 3]);
}

std::string profile_summary(const fs::path& path, const std::vector<unsigned char>& bytes) {
    const std::string extension = lower(path.extension().string());
    if (extension == ".jpg" || extension == ".jpeg") {
        std::size_t offset = 2;
        while (offset + 4 <= bytes.size() && bytes[offset] == 0xff) {
            const unsigned char marker = bytes[offset + 1];
            offset += 2;
            if (marker == 0xda || marker == 0xd9) break;
            if (marker == 0xd8 || marker == 0x01 || (marker >= 0xd0 && marker <= 0xd7)) continue;
            const std::size_t length = (static_cast<std::size_t>(bytes[offset]) << 8) | bytes[offset + 1];
            if (length < 2 || length > bytes.size() - offset) break;
            if (marker == 0xe2 && length >= 16 &&
                starts_with(bytes, offset + 2, "ICC_PROFILE\0", 12)) return "Embedded ICC profile";
            offset += length;
        }
    } else if (extension == ".png") {
        std::size_t offset = 8;
        while (offset + 12 <= bytes.size()) {
            const std::uint32_t length = read32(bytes, offset, false);
            if (length > bytes.size() - offset - 12) break;
            if (starts_with(bytes, offset + 4, "iCCP", 4)) return "Embedded ICC profile";
            if (starts_with(bytes, offset + 4, "sRGB", 4)) return "sRGB profile";
            offset += static_cast<std::size_t>(length) + 12;
        }
    } else if (extension == ".webp" && bytes.size() >= 12 &&
               starts_with(bytes, 0, "RIFF", 4)) {
        for (std::size_t offset = 12; offset + 8 <= bytes.size();) {
            const std::uint32_t length = read32(bytes, offset + 4, true);
            if (starts_with(bytes, offset, "ICCP", 4)) return "Embedded ICC profile";
            if (length > bytes.size() - offset - 8) break;
            offset += 8 + length + (length & 1);
        }
    }
    return "Not embedded (decoder default)";
}

std::vector<unsigned char> read_file(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    input.seekg(0, std::ios::end);
    const auto size = input.tellg();
    if (size < 0 || size > 128 * 1024 * 1024) return {};
    input.seekg(0, std::ios::beg);
    return std::vector<unsigned char>(std::istreambuf_iterator<char>(input), {});
}

}  // namespace

std::vector<std::string> available_extensions() {
    std::vector<std::string> extensions;
    GSList* formats = gdk_pixbuf_get_formats();
    for (GSList* node = formats; node; node = node->next) {
        auto* format = static_cast<GdkPixbufFormat*>(node->data);
        gchar** names = gdk_pixbuf_format_get_extensions(format);
        for (gchar** name = names; name && *name; ++name) {
            extensions.push_back("." + lower(*name));
        }
        g_strfreev(names);
    }
    g_slist_free(formats);
    extensions.emplace_back(".webp");
    std::sort(extensions.begin(), extensions.end());
    extensions.erase(std::unique(extensions.begin(), extensions.end()), extensions.end());
    return extensions;
}

std::vector<std::string> available_formats() {
    std::vector<std::string> formats;
    GSList* loaders = gdk_pixbuf_get_formats();
    for (GSList* node = loaders; node; node = node->next) {
        auto* format = static_cast<GdkPixbufFormat*>(node->data);
        gchar* name = gdk_pixbuf_format_get_name(format);
        if (name) formats.emplace_back(name);
        g_free(name);
    }
    g_slist_free(loaders);
    formats.emplace_back("webp");
    std::sort(formats.begin(), formats.end());
    return formats;
}

bool can_decode(const fs::path& path) {
    const std::string extension = lower(path.extension().string());
    const auto extensions = available_extensions();
    return std::find(extensions.begin(), extensions.end(), extension) != extensions.end();
}

std::string encoder_for_extension(const fs::path& path) {
    const std::string extension = lower(path.extension().string());
    if (extension == ".webp") return "webp";
    GSList* formats = gdk_pixbuf_get_formats();
    std::string result;
    for (GSList* node = formats; node && result.empty(); node = node->next) {
        auto* format = static_cast<GdkPixbufFormat*>(node->data);
        if (!gdk_pixbuf_format_is_writable(format)) continue;
        gchar** names = gdk_pixbuf_format_get_extensions(format);
        for (gchar** name = names; name && *name; ++name) {
            if ("." + lower(*name) == extension) {
                gchar* format_name = gdk_pixbuf_format_get_name(format);
                if (format_name) result = format_name;
                g_free(format_name);
                break;
            }
        }
        g_strfreev(names);
    }
    g_slist_free(formats);
    return result;
}

bool save_webp(const fs::path& path, GdkPixbuf* pixbuf, std::string& error) {
    if (!pixbuf) {
        error = "No image pixels are available.";
        return false;
    }
    const int width = gdk_pixbuf_get_width(pixbuf);
    const int height = gdk_pixbuf_get_height(pixbuf);
    const int rowstride = gdk_pixbuf_get_rowstride(pixbuf);
    const guchar* pixels = gdk_pixbuf_get_pixels(pixbuf);
    uint8_t* encoded = nullptr;
    const std::size_t encoded_size = gdk_pixbuf_get_has_alpha(pixbuf)
        ? WebPEncodeRGBA(pixels, width, height, rowstride, 90.0f, &encoded)
        : WebPEncodeRGB(pixels, width, height, rowstride, 90.0f, &encoded);
    if (!encoded_size || !encoded) {
        error = "libwebp could not encode this image.";
        return false;
    }
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        error = "Could not open the output file for writing.";
        WebPFree(encoded);
        return false;
    }
    output.write(reinterpret_cast<const char*>(encoded), static_cast<std::streamsize>(encoded_size));
    WebPFree(encoded);
    if (!output) {
        error = "An error occurred while writing the WebP file.";
        return false;
    }
    return true;
}

std::vector<fs::path> images_in_directory(const fs::path& directory) {
    std::vector<fs::path> paths;
    std::error_code error;
    for (fs::directory_iterator it(directory, error), end; !error && it != end; it.increment(error)) {
        if (it->is_regular_file(error) && can_decode(it->path())) paths.push_back(it->path());
    }
    std::sort(paths.begin(), paths.end(), [](const fs::path& a, const fs::path& b) {
        return lower(a.filename().string()) < lower(b.filename().string());
    });
    return paths;
}

Metadata read_metadata(const fs::path& path, GdkPixbuf* pixbuf, bool animated) {
    Metadata metadata;
    if (pixbuf) {
        metadata.width = gdk_pixbuf_get_width(pixbuf);
        metadata.height = gdk_pixbuf_get_height(pixbuf);
        metadata.has_alpha = gdk_pixbuf_get_has_alpha(pixbuf);
    }
    metadata.animated = animated;
    GdkPixbufFormat* format = gdk_pixbuf_get_file_info(path.string().c_str(), nullptr, nullptr);
    if (format) {
        gchar* name = gdk_pixbuf_format_get_name(format);
        if (name) metadata.format = name;
        g_free(name);
    }
    if (metadata.format.empty() && lower(path.extension().string()) == ".webp") metadata.format = "webp";
    std::error_code error;
    metadata.file_size = fs::file_size(path, error);
    if (error) metadata.file_size = 0;
    const auto bytes = read_file(path);
    metadata.exif = exif_summary(bytes);
    metadata.color_profile = profile_summary(path, bytes);
    return metadata;
}

GdkPixbuf* rotate(GdkPixbuf* pixbuf, int degrees) {
    if (!pixbuf) return nullptr;
    GdkPixbufRotation rotation;
    switch ((degrees % 360 + 360) % 360) {
        case 90: rotation = GDK_PIXBUF_ROTATE_CLOCKWISE; break;
        case 180: rotation = GDK_PIXBUF_ROTATE_UPSIDEDOWN; break;
        case 270: rotation = GDK_PIXBUF_ROTATE_COUNTERCLOCKWISE; break;
        default: return gdk_pixbuf_copy(pixbuf);
    }
    return gdk_pixbuf_rotate_simple(pixbuf, rotation);
}

GdkPixbuf* flip(GdkPixbuf* pixbuf, bool horizontal) {
    return pixbuf ? gdk_pixbuf_flip(pixbuf, horizontal) : nullptr;
}

GdkPixbuf* crop(GdkPixbuf* pixbuf, CropRect rect) {
    if (!pixbuf || rect.x < 0 || rect.y < 0 || rect.width <= 0 || rect.height <= 0 ||
        rect.x > gdk_pixbuf_get_width(pixbuf) - rect.width ||
        rect.y > gdk_pixbuf_get_height(pixbuf) - rect.height) {
        return nullptr;
    }
    GdkPixbuf* subpixbuf = gdk_pixbuf_new_subpixbuf(pixbuf, rect.x, rect.y, rect.width, rect.height);
    if (!subpixbuf) return nullptr;
    GdkPixbuf* result = gdk_pixbuf_copy(subpixbuf);
    g_object_unref(subpixbuf);
    return result;
}

GdkPixbuf* load_pixbuf(const fs::path& path, GError** error) {
    if (lower(path.extension().string()) == ".webp") {
        gchar* contents = nullptr;
        gsize length = 0;
        if (!g_file_get_contents(path.string().c_str(), &contents, &length, error)) return nullptr;
        int width = 0;
        int height = 0;
        guchar* pixels = WebPDecodeRGBA(reinterpret_cast<const uint8_t*>(contents),
                                        static_cast<std::size_t>(length), &width, &height);
        g_free(contents);
        if (!pixels) {
            g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED, "libwebp could not decode this WebP image.");
            return nullptr;
        }
        GdkPixbuf* decoded = gdk_pixbuf_new_from_data(pixels, GDK_COLORSPACE_RGB, TRUE, 8,
            width, height, width * 4, release_webp_pixels, nullptr);
        if (!decoded) {
            WebPFree(pixels);
            g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED, "Could not allocate decoded WebP pixels.");
        }
        return decoded;
    }
    GdkPixbuf* pixbuf = gdk_pixbuf_new_from_file(path.string().c_str(), error);
    if (!pixbuf) return nullptr;
    GdkPixbuf* oriented = gdk_pixbuf_apply_embedded_orientation(pixbuf);
    g_object_unref(pixbuf);
    return oriented;
}

std::string supported_format_list() {
    const auto formats = available_extensions();
    std::ostringstream output;
    for (std::size_t i = 0; i < formats.size(); ++i) {
        if (i) output << ", ";
        output << formats[i];
    }
    return output.str();
}

}  // namespace image
