#include "image_core.hpp"

#include <cassert>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

int main() {
    const fs::path test_dir = fs::current_path() / "test-data";
    fs::remove_all(test_dir);
    fs::create_directories(test_dir);

    GdkPixbuf* source = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, 4, 3);
    assert(source);
    gdk_pixbuf_fill(source, 0xff0000ff);
    const fs::path first = test_dir / "a.png";
    const fs::path second = test_dir / "b.png";
    const fs::path webp = test_dir / "sample.webp";
    GError* error = nullptr;
    assert(gdk_pixbuf_save(source, first.string().c_str(), "png", &error, nullptr));
    assert(gdk_pixbuf_save(source, second.string().c_str(), "png", &error, nullptr));
    std::string webp_error;
    assert(image::save_webp(webp, source, webp_error));
    assert(!error);

    assert(image::can_decode(first));
    assert(image::can_decode(webp));
    assert(!image::can_decode(test_dir / "unknown.xyz"));
    const auto files = image::images_in_directory(test_dir);
    assert(files.size() == 3 && files[0].filename() == "a.png" &&
           files[1].filename() == "b.png" && files[2].filename() == "sample.webp");

    GdkPixbuf* rotated = image::rotate(source, 90);
    assert(rotated && gdk_pixbuf_get_width(rotated) == 3 && gdk_pixbuf_get_height(rotated) == 4);
    GdkPixbuf* flipped = image::flip(source, true);
    assert(flipped && gdk_pixbuf_get_width(flipped) == 4);
    GdkPixbuf* cropped = image::crop(source, {1, 1, 2, 1});
    assert(cropped && gdk_pixbuf_get_width(cropped) == 2 && gdk_pixbuf_get_height(cropped) == 1);
    assert(!image::crop(source, {3, 2, 2, 2}));

    GdkPixbuf* loaded = image::load_pixbuf(first, &error);
    assert(loaded && !error);
    const image::Metadata metadata = image::read_metadata(first, loaded, false);
    assert(metadata.width == 4 && metadata.height == 3);
    assert(metadata.has_alpha);
    assert(metadata.file_size > 0);
    assert(metadata.format == "png");
    assert(image::encoder_for_extension("sample.png") == "png");
    assert(image::encoder_for_extension("sample.svg").empty());
    assert(image::encoder_for_extension("sample.webp") == "webp");
    GdkPixbuf* webp_loaded = image::load_pixbuf(webp, &error);
    assert(webp_loaded && !error);
    assert(gdk_pixbuf_get_width(webp_loaded) == 4 && gdk_pixbuf_get_height(webp_loaded) == 3);
    const image::Metadata webp_metadata = image::read_metadata(webp, webp_loaded, false);
    assert(webp_metadata.format == "webp");

    g_object_unref(webp_loaded);
    assert(!image::available_extensions().empty());
    assert(!image::available_formats().empty());

    g_object_unref(loaded);
    g_object_unref(cropped);
    g_object_unref(flipped);
    g_object_unref(rotated);
    g_object_unref(source);
    fs::remove_all(test_dir);
    std::cout << "Available image decoders: " << image::supported_format_list() << '\n'
              << "image_core tests passed\n";
    return 0;
}
