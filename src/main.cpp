#include "image_core.hpp"

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

struct Viewer {
    GtkWidget* window = nullptr;
    GtkWidget* drawing = nullptr;
    GtkWidget* status = nullptr;
    GtkWidget* info = nullptr;
    GtkWidget* info_text = nullptr;
    GdkPixbuf* pixbuf = nullptr;
    GdkPixbuf* frame = nullptr;
    GdkPixbufAnimation* animation = nullptr;
    GdkPixbufAnimationIter* animation_iter = nullptr;
    std::vector<fs::path> paths;
    std::size_t index = 0;
    guint animation_timer = 0;
    guint slideshow_timer = 0;
    bool fit = true;
    bool slideshow = false;
    bool crop_mode = false;
    bool selecting = false;
    bool has_selection = false;
    double zoom = 1.0;
    double display_scale = 1.0;
    double image_left = 0.0;
    double image_top = 0.0;
    double start_x = 0.0;
    double start_y = 0.0;
    double selection_x = 0.0;
    double selection_y = 0.0;
    double selection_width = 0.0;
    double selection_height = 0.0;
};

namespace {

GdkPixbuf* displayed_pixbuf(Viewer* viewer) {
    return viewer->animation ? viewer->frame : viewer->pixbuf;
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
GTimeVal animation_time() {
    const gint64 now = g_get_real_time();
    return {static_cast<glong>(now / G_USEC_PER_SEC),
            static_cast<glong>(now % G_USEC_PER_SEC)};
}
GdkPixbufAnimationIter* start_animation(GdkPixbufAnimation* animation) {
    GTimeVal now = animation_time();
    return gdk_pixbuf_animation_get_iter(animation, &now);
}
gboolean advance_animation(GdkPixbufAnimationIter* iter) {
    GTimeVal now = animation_time();
    return gdk_pixbuf_animation_iter_advance(iter, &now);
}
#pragma GCC diagnostic pop

void set_status(Viewer* viewer, const std::string& text) {
    gtk_label_set_text(GTK_LABEL(viewer->status), text.c_str());
}

void show_error(Viewer* viewer, const std::string& title, const std::string& text) {
    GtkWidget* dialog = gtk_message_dialog_new(GTK_WINDOW(viewer->window),
        GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, "%s", title.c_str());
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "%s", text.c_str());
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

void show_info_dialog(Viewer* viewer, const std::string& title, const std::string& text) {
    GtkWidget* dialog = gtk_message_dialog_new(GTK_WINDOW(viewer->window),
        GTK_DIALOG_MODAL, GTK_MESSAGE_INFO, GTK_BUTTONS_CLOSE, "%s", title.c_str());
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "%s", text.c_str());
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

void update_info(Viewer* viewer) {
    if (viewer->paths.empty() || !displayed_pixbuf(viewer)) {
        gtk_label_set_text(GTK_LABEL(viewer->info_text),
            "Open an image to see its dimensions, file size, transparency, EXIF and color profile.");
        return;
    }
    const auto& path = viewer->paths[viewer->index];
    const image::Metadata meta = image::read_metadata(path, displayed_pixbuf(viewer), viewer->animation != nullptr);
    gchar* size = g_format_size(meta.file_size);
    std::string details =
        "File: " + path.filename().string() +
        "\nFormat: " + (meta.format.empty() ? "unknown" : meta.format) +
        "\nDimensions: " + std::to_string(meta.width) + " × " + std::to_string(meta.height) +
        "\nFile size: " + (size ? size : "unknown") +
        "\nTransparency: " + (meta.has_alpha ? "yes" : "no") +
        "\nAnimation: " + (meta.animated ? "yes (animated frames)" : "no") +
        "\nColor profile: " + meta.color_profile +
        "\n\nEXIF\n" + meta.exif;
    g_free(size);
    gtk_label_set_text(GTK_LABEL(viewer->info_text), details.c_str());
}

void update_title(Viewer* viewer) {
    if (viewer->paths.empty()) {
        gtk_window_set_title(GTK_WINDOW(viewer->window), "GnuChan Image Viewer");
        return;
    }
    const std::string title = viewer->paths[viewer->index].filename().string() +
                              " — GnuChan Image Viewer";
    gtk_window_set_title(GTK_WINDOW(viewer->window), title.c_str());
}

gboolean animation_tick(gpointer data) {
    auto* viewer = static_cast<Viewer*>(data);
    if (!viewer->animation_iter) return G_SOURCE_REMOVE;
    if (advance_animation(viewer->animation_iter)) {
        GdkPixbuf* next = gdk_pixbuf_animation_iter_get_pixbuf(viewer->animation_iter);
        if (next) {
            if (viewer->frame) g_object_unref(viewer->frame);
            viewer->frame = GDK_PIXBUF(g_object_ref(next));
            gtk_widget_queue_draw(viewer->drawing);
        }
    }
    return G_SOURCE_CONTINUE;
}

bool load_current(Viewer* viewer) {
    if (viewer->index >= viewer->paths.size()) return false;
    if (viewer->animation_timer) {
        g_source_remove(viewer->animation_timer);
        viewer->animation_timer = 0;
    }
    if (viewer->animation_iter) {
        g_object_unref(viewer->animation_iter);
        viewer->animation_iter = nullptr;
    }
    if (viewer->animation) {
        g_object_unref(viewer->animation);
        viewer->animation = nullptr;
    }
    if (viewer->pixbuf) {
        g_object_unref(viewer->pixbuf);
        viewer->pixbuf = nullptr;
    }
    if (viewer->frame) {
        g_object_unref(viewer->frame);
        viewer->frame = nullptr;
    }
    const fs::path& path = viewer->paths[viewer->index];
    if (!image::can_decode(path)) {
        show_error(viewer, "Image format unavailable",
                   "No installed image decoder supports " + path.extension().string() +
                   ". Available extensions: " + image::supported_format_list());
        return false;
    }
    GError* error = nullptr;
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    if (extension == ".webp") {
        viewer->pixbuf = image::load_pixbuf(path, &error);
        if (!viewer->pixbuf) {
            const std::string message = error ? error->message : "libwebp could not load this image.";
            if (error) g_error_free(error);
            show_error(viewer, "Could not open image", message);
            return false;
        }
    } else {
        viewer->animation = gdk_pixbuf_animation_new_from_file(path.string().c_str(), &error);
        if (!viewer->animation) {
            const std::string message = error ? error->message : "The image decoder could not load this file.";
            if (error) g_error_free(error);
            viewer->animation = nullptr;
            show_error(viewer, "Could not open image", message);
            return false;
        }
        if (gdk_pixbuf_animation_is_static_image(viewer->animation)) {
            GdkPixbuf* source = gdk_pixbuf_animation_get_static_image(viewer->animation);
            viewer->pixbuf = source ? gdk_pixbuf_apply_embedded_orientation(source) : nullptr;
            g_object_unref(viewer->animation);
            viewer->animation = nullptr;
            if (!viewer->pixbuf) {
                show_error(viewer, "Could not open image", "The installed decoder returned no image data.");
                return false;
            }
        } else {
            viewer->animation_iter = start_animation(viewer->animation);
            GdkPixbuf* first = gdk_pixbuf_animation_iter_get_pixbuf(viewer->animation_iter);
            if (first) viewer->frame = GDK_PIXBUF(g_object_ref(first));
            viewer->animation_timer = g_timeout_add(20, animation_tick, viewer);
        }
    }
    viewer->fit = true;
    viewer->zoom = 1.0;
    viewer->crop_mode = false;
    viewer->has_selection = false;
    update_title(viewer);
    update_info(viewer);
    gtk_widget_queue_draw(viewer->drawing);
    set_status(viewer, path.string());
    return true;
}

void populate_directory(Viewer* viewer, const fs::path& directory, const fs::path& selected) {
    if (!selected.empty() && !image::can_decode(selected)) {
        show_error(viewer, "Image format unavailable",
            "No installed image decoder supports " + selected.extension().string() +
            ". Available extensions: " + image::supported_format_list());
        return;
    }
    viewer->paths = image::images_in_directory(directory);
    if (viewer->paths.empty()) {
        show_info_dialog(viewer, "No supported images",
            "This directory has no files with an installed image decoder. Available extensions: " +
            image::supported_format_list());
        return;
    }
    auto found = std::find(viewer->paths.begin(), viewer->paths.end(), selected);
    viewer->index = found == viewer->paths.end()
        ? 0 : static_cast<std::size_t>(std::distance(viewer->paths.begin(), found));
    load_current(viewer);
}

void open_file(Viewer* viewer) {
    GtkWidget* dialog = gtk_file_chooser_dialog_new("Open image",
        GTK_WINDOW(viewer->window), GTK_FILE_CHOOSER_ACTION_OPEN,
        "_Cancel", GTK_RESPONSE_CANCEL, "_Open", GTK_RESPONSE_ACCEPT, nullptr);
    GtkFileFilter* filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "Images supported by installed decoders");
    for (const auto& extension : image::available_extensions()) {
        gtk_file_filter_add_pattern(filter, ("*" + extension).c_str());
        std::string upper = extension;
        std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char ch) {
            return static_cast<char>(std::toupper(ch));
        });
        gtk_file_filter_add_pattern(filter, ("*" + upper).c_str());
    }
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);
    gtk_file_chooser_set_filter(GTK_FILE_CHOOSER(dialog), filter);
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), gtk_file_filter_new());
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        gchar* filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        if (filename) {
            const fs::path path(filename);
            populate_directory(viewer, path.parent_path(), path);
            g_free(filename);
        }
    }
    gtk_widget_destroy(dialog);
}

void open_directory(Viewer* viewer) {
    GtkWidget* dialog = gtk_file_chooser_dialog_new("Open image directory",
        GTK_WINDOW(viewer->window), GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
        "_Cancel", GTK_RESPONSE_CANCEL, "_Open", GTK_RESPONSE_ACCEPT, nullptr);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        gchar* folder = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        if (folder) {
            populate_directory(viewer, fs::path(folder), {});
            g_free(folder);
        }
    }
    gtk_widget_destroy(dialog);
}

void navigate(Viewer* viewer, int delta) {
    if (viewer->paths.empty()) return;
    const auto count = static_cast<long long>(viewer->paths.size());
    long long next = (static_cast<long long>(viewer->index) + delta) % count;
    if (next < 0) next += count;
    viewer->index = static_cast<std::size_t>(next);
    load_current(viewer);
}

void apply_transform(Viewer* viewer, GdkPixbuf* transformed) {
    if (!transformed) return;
    if (viewer->animation_timer) {
        g_source_remove(viewer->animation_timer);
        viewer->animation_timer = 0;
    }
    if (viewer->animation_iter) {
        g_object_unref(viewer->animation_iter);
        viewer->animation_iter = nullptr;
    }
    if (viewer->animation) {
        g_object_unref(viewer->animation);
        viewer->animation = nullptr;
    }
    if (viewer->frame) {
        g_object_unref(viewer->frame);
        viewer->frame = nullptr;
    }
    if (viewer->pixbuf) g_object_unref(viewer->pixbuf);
    viewer->pixbuf = transformed;
    viewer->has_selection = false;
    viewer->crop_mode = false;
    update_info(viewer);
    gtk_widget_queue_draw(viewer->drawing);
    set_status(viewer, "Edited in memory. Use Save As to write the result.");
}

void rotate_image(Viewer* viewer, bool clockwise) {
    GdkPixbuf* source = displayed_pixbuf(viewer);
    if (!source) return;
    apply_transform(viewer, image::rotate(source, clockwise ? 90 : 270));
}

void flip_image(Viewer* viewer, bool horizontal) {
    GdkPixbuf* source = displayed_pixbuf(viewer);
    if (!source) return;
    apply_transform(viewer, image::flip(source, horizontal));
}

void set_zoom(Viewer* viewer, double factor) {
    viewer->fit = false;
    viewer->zoom = std::clamp(viewer->zoom * factor, 0.05, 16.0);
    gtk_widget_queue_draw(viewer->drawing);
}

void toggle_slideshow(Viewer* viewer) {
    if (viewer->slideshow_timer) {
        g_source_remove(viewer->slideshow_timer);
        viewer->slideshow_timer = 0;
        viewer->slideshow = false;
        set_status(viewer, "Slideshow stopped");
    } else if (viewer->paths.size() > 1) {
        viewer->slideshow = true;
        viewer->slideshow_timer = g_timeout_add_seconds(3, [](gpointer data) -> gboolean {
            auto* app = static_cast<Viewer*>(data);
            navigate(app, 1);
            return G_SOURCE_CONTINUE;
        }, viewer);
        set_status(viewer, "Slideshow running — press Space to stop");
    }
}

void save_as(Viewer* viewer) {
    GdkPixbuf* source = displayed_pixbuf(viewer);
    if (!source || viewer->paths.empty()) return;
    GtkWidget* dialog = gtk_file_chooser_dialog_new("Save edited image",
        GTK_WINDOW(viewer->window), GTK_FILE_CHOOSER_ACTION_SAVE,
        "_Cancel", GTK_RESPONSE_CANCEL, "_Save", GTK_RESPONSE_ACCEPT, nullptr);
    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dialog), TRUE);
    fs::path output = viewer->paths[viewer->index];
    output.replace_extension(".png");
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog), output.filename().string().c_str());
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        gchar* filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        if (filename) {
            const fs::path path(filename);
            const std::string format = image::encoder_for_extension(path);
            GError* error = nullptr;
            if (format.empty()) {
                show_error(viewer, "Output format unavailable",
                    "No installed GdkPixbuf encoder supports this filename extension. Decodable extensions: " +
                    image::supported_format_list());
            } else if (!gdk_pixbuf_save(source, path.string().c_str(), format.c_str(), &error, nullptr)) {
                const std::string message = error ? error->message : "The selected encoder could not save this image.";
                if (error) g_error_free(error);
                show_error(viewer, "Could not save image", message);
            } else {
                set_status(viewer, "Saved " + path.string() + " (embedded color profiles are not preserved by GdkPixbuf export).");
            }
            g_free(filename);
        }
    }
    gtk_widget_destroy(dialog);
}

void copy_to_clipboard(Viewer* viewer) {
    GdkPixbuf* source = displayed_pixbuf(viewer);
    if (!source) return;
    GtkClipboard* clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
    gtk_clipboard_set_image(clipboard, source);
    gtk_clipboard_store(clipboard);
    set_status(viewer, "Image copied to clipboard");
}

void set_wallpaper(Viewer* viewer) {
    if (viewer->paths.empty()) return;
    gchar* uri = g_filename_to_uri(viewer->paths[viewer->index].string().c_str(), nullptr, nullptr);
    if (!uri) {
        show_error(viewer, "Wallpaper unavailable", "Could not convert this path to a file URI.");
        return;
    }
    const char* desktop = g_getenv("XDG_CURRENT_DESKTOP");
    gchar* output = nullptr;
    gint status = 0;
    GError* error = nullptr;
    gchar* gnome_args[] = {const_cast<gchar*>("gsettings"), const_cast<gchar*>("set"),
        const_cast<gchar*>("org.gnome.desktop.background"), const_cast<gchar*>("picture-uri"),
        uri, nullptr};
    gboolean success = desktop && (g_strrstr(desktop, "GNOME") || g_strrstr(desktop, "gnome"))
        ? g_spawn_sync(nullptr, gnome_args, nullptr, G_SPAWN_SEARCH_PATH, nullptr, nullptr,
                       &output, nullptr, &status, &error)
        : FALSE;
    if (success && status == 0) {
        set_status(viewer, "Desktop wallpaper updated");
    } else {
        if (error) g_error_free(error);
        error = nullptr;
        const std::string filename = viewer->paths[viewer->index].string();
        gchar* feh_args[] = {const_cast<gchar*>("feh"), const_cast<gchar*>("--bg-fill"),
            const_cast<gchar*>(filename.c_str()), nullptr};
        success = g_spawn_sync(nullptr, feh_args, nullptr, G_SPAWN_SEARCH_PATH, nullptr, nullptr,
                               &output, nullptr, &status, &error);
        if (success && status == 0) set_status(viewer, "Desktop wallpaper updated");
        else show_info_dialog(viewer, "Wallpaper command unavailable",
            "Wallpaper integration is available on GNOME (gsettings) or when feh is installed. No shell command was run.");
    }
    if (error) g_error_free(error);
    g_free(output);
    g_free(uri);
}

void rename_file(Viewer* viewer) {
    if (viewer->paths.empty()) return;
    const fs::path original = viewer->paths[viewer->index];
    GtkWidget* dialog = gtk_dialog_new_with_buttons("Rename image", GTK_WINDOW(viewer->window),
        GTK_DIALOG_MODAL, "_Cancel", GTK_RESPONSE_CANCEL, "_Rename", GTK_RESPONSE_ACCEPT, nullptr);
    GtkWidget* entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(entry), original.filename().string().c_str());
    gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), entry);
    gtk_widget_show_all(dialog);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        const std::string name = gtk_entry_get_text(GTK_ENTRY(entry));
        const fs::path new_name(name);
        if (name.empty() || new_name.has_parent_path() || new_name.filename() != new_name) {
            show_error(viewer, "Invalid filename", "Enter a filename only, without directory separators.");
        } else {
            const fs::path target = original.parent_path() / new_name;
            if (!image::can_decode(target)) {
                show_error(viewer, "Image format unavailable",
                    "The new filename extension has no installed image decoder. Available extensions: " +
                    image::supported_format_list());
                gtk_widget_destroy(dialog);
                return;
            }
            std::error_code error;
            fs::rename(original, target, error);
            if (error) {
                show_error(viewer, "Could not rename image", error.message());
            } else {
                populate_directory(viewer, target.parent_path(), target);
            }
        }
    }
    gtk_widget_destroy(dialog);
}

void delete_file(Viewer* viewer) {
    if (viewer->paths.empty()) return;
    const fs::path path = viewer->paths[viewer->index];
    GtkWidget* dialog = gtk_message_dialog_new(GTK_WINDOW(viewer->window), GTK_DIALOG_MODAL,
        GTK_MESSAGE_WARNING, GTK_BUTTONS_NONE, "Delete %s?", path.filename().string().c_str());
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "This permanently removes the file.");
    gtk_dialog_add_buttons(GTK_DIALOG(dialog), "_Cancel", GTK_RESPONSE_CANCEL,
                           "_Delete", GTK_RESPONSE_ACCEPT, nullptr);
    const gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    if (response == GTK_RESPONSE_ACCEPT) {
        std::error_code error;
        fs::remove(path, error);
        if (error) show_error(viewer, "Could not delete image", error.message());
        else populate_directory(viewer, path.parent_path(), {});
    }
}

void toggle_info(Viewer* viewer) {
    if (gtk_widget_get_visible(viewer->info)) gtk_widget_hide(viewer->info);
    else gtk_widget_show(viewer->info);
}

void toggle_fullscreen(Viewer* viewer) {
    GdkWindow* window = gtk_widget_get_window(viewer->window);
    if (window && (gdk_window_get_state(window) & GDK_WINDOW_STATE_FULLSCREEN))
        gtk_window_unfullscreen(GTK_WINDOW(viewer->window));
    else
        gtk_window_fullscreen(GTK_WINDOW(viewer->window));
}

void toggle_crop(Viewer* viewer) {
    if (!displayed_pixbuf(viewer)) return;
    viewer->crop_mode = !viewer->crop_mode;
    viewer->has_selection = false;
    GdkWindow* window = gtk_widget_get_window(viewer->drawing);
    if (window) {
        GdkCursor* cursor = gdk_cursor_new_from_name(gtk_widget_get_display(viewer->drawing),
            viewer->crop_mode ? "crosshair" : "default");
        gdk_window_set_cursor(window, cursor);
        if (cursor) g_object_unref(cursor);
    }
    set_status(viewer, viewer->crop_mode ? "Drag a crop rectangle, then press Enter" : "Crop selection cancelled");
}

void apply_crop(Viewer* viewer) {
    GdkPixbuf* source = displayed_pixbuf(viewer);
    if (!source || !viewer->has_selection || viewer->display_scale <= 0.0) return;
    const int width = gdk_pixbuf_get_width(source);
    const int height = gdk_pixbuf_get_height(source);
    const int x = std::clamp(static_cast<int>(std::floor((viewer->selection_x - viewer->image_left) / viewer->display_scale)), 0, width - 1);
    const int y = std::clamp(static_cast<int>(std::floor((viewer->selection_y - viewer->image_top) / viewer->display_scale)), 0, height - 1);
    const int right = std::clamp(static_cast<int>(std::ceil((viewer->selection_x + viewer->selection_width - viewer->image_left) / viewer->display_scale)), x + 1, width);
    const int bottom = std::clamp(static_cast<int>(std::ceil((viewer->selection_y + viewer->selection_height - viewer->image_top) / viewer->display_scale)), y + 1, height);
    apply_transform(viewer, image::crop(source, {x, y, right - x, bottom - y}));
}

gboolean draw_image(GtkWidget* widget, cairo_t* cr, gpointer data) {
    auto* viewer = static_cast<Viewer*>(data);
    const int width = gtk_widget_get_allocated_width(widget);
    const int height = gtk_widget_get_allocated_height(widget);
    constexpr int tile = 14;
    for (int y = 0; y < height; y += tile) {
        for (int x = 0; x < width; x += tile) {
            const bool light = ((x / tile) + (y / tile)) % 2 == 0;
            cairo_set_source_rgb(cr, light ? 0.88 : 0.74, light ? 0.88 : 0.74, light ? 0.88 : 0.74);
            cairo_rectangle(cr, x, y, tile, tile);
            cairo_fill(cr);
        }
    }
    GdkPixbuf* source = displayed_pixbuf(viewer);
    if (!source) {
        cairo_set_source_rgb(cr, 0.15, 0.15, 0.15);
        cairo_select_font_face(cr, "sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 16);
        cairo_move_to(cr, 28, 42);
        cairo_show_text(cr, "Open an image or drag it here");
        return FALSE;
    }
    const int image_width = gdk_pixbuf_get_width(source);
    const int image_height = gdk_pixbuf_get_height(source);
    const double scale = viewer->fit
        ? std::min(static_cast<double>(width) / image_width, static_cast<double>(height) / image_height) * viewer->zoom
        : viewer->zoom;
    const int scaled_width = std::max(1, static_cast<int>(std::round(image_width * scale)));
    const int scaled_height = std::max(1, static_cast<int>(std::round(image_height * scale)));
    viewer->display_scale = scale;
    viewer->image_left = (width - scaled_width) / 2.0;
    viewer->image_top = (height - scaled_height) / 2.0;
    GdkPixbuf* scaled = scaled_width == image_width && scaled_height == image_height
        ? GDK_PIXBUF(g_object_ref(source))
        : gdk_pixbuf_scale_simple(source, scaled_width, scaled_height, GDK_INTERP_BILINEAR);
    if (scaled) {
        gdk_cairo_set_source_pixbuf(cr, scaled, viewer->image_left, viewer->image_top);
        cairo_paint(cr);
        g_object_unref(scaled);
    }
    if (viewer->crop_mode && viewer->has_selection) {
        cairo_set_source_rgba(cr, 0.1, 0.55, 1.0, 0.95);
        cairo_set_line_width(cr, 2.0);
        cairo_rectangle(cr, viewer->selection_x, viewer->selection_y,
                        viewer->selection_width, viewer->selection_height);
        cairo_stroke(cr);
    }
    return FALSE;
}

gboolean button_press(GtkWidget*, GdkEventButton* event, gpointer data) {
    auto* viewer = static_cast<Viewer*>(data);
    if (viewer->crop_mode && event->button == 1) {
        viewer->selecting = true;
        viewer->has_selection = true;
        viewer->start_x = event->x;
        viewer->start_y = event->y;
        viewer->selection_x = event->x;
        viewer->selection_y = event->y;
        viewer->selection_width = viewer->selection_height = 0;
        return TRUE;
    }
    return FALSE;
}

gboolean motion(GtkWidget* widget, GdkEventMotion* event, gpointer data) {
    auto* viewer = static_cast<Viewer*>(data);
    if (viewer->selecting) {
        viewer->selection_x = std::min(viewer->start_x, event->x);
        viewer->selection_y = std::min(viewer->start_y, event->y);
        viewer->selection_width = std::abs(event->x - viewer->start_x);
        viewer->selection_height = std::abs(event->y - viewer->start_y);
        gtk_widget_queue_draw(widget);
    }
    return TRUE;
}

gboolean button_release(GtkWidget*, GdkEventButton* event, gpointer data) {
    auto* viewer = static_cast<Viewer*>(data);
    if (viewer->selecting && event->button == 1) {
        viewer->selecting = false;
        if (viewer->selection_width < 3 || viewer->selection_height < 3) {
            viewer->has_selection = false;
            set_status(viewer, "Crop selection too small");
        } else {
            set_status(viewer, "Press Enter to apply crop, Escape to cancel");
        }
        return TRUE;
    }
    return FALSE;
}

void dropped(GtkWidget*, GdkDragContext* context, gint, gint, GtkSelectionData* selection,
             guint, guint time, gpointer data) {
    auto* viewer = static_cast<Viewer*>(data);
    gchar** uris = gtk_selection_data_get_uris(selection);
    if (uris && uris[0]) {
        gchar* filename = g_filename_from_uri(uris[0], nullptr, nullptr);
        if (filename) {
            const fs::path path(filename);
            if (fs::is_directory(path)) populate_directory(viewer, path, {});
            else populate_directory(viewer, path.parent_path(), path);
            g_free(filename);
        }
    }
    g_strfreev(uris);
    gtk_drag_finish(context, TRUE, FALSE, time);
}

gboolean key_press(GtkWidget*, GdkEventKey* event, gpointer data) {
    auto* viewer = static_cast<Viewer*>(data);
    const bool control = (event->state & GDK_CONTROL_MASK) != 0;
    if (control) {
        switch (event->keyval) {
            case GDK_KEY_o: open_file(viewer); return TRUE;
            case GDK_KEY_s: save_as(viewer); return TRUE;
            case GDK_KEY_c: copy_to_clipboard(viewer); return TRUE;
            default: break;
        }
    }
    switch (event->keyval) {
        case GDK_KEY_Escape:
            if (viewer->crop_mode) toggle_crop(viewer);
            else {
                GdkWindow* window = gtk_widget_get_window(viewer->window);
                if (window && (gdk_window_get_state(window) & GDK_WINDOW_STATE_FULLSCREEN))
                    toggle_fullscreen(viewer);
            }
            return TRUE;
        case GDK_KEY_F11: toggle_fullscreen(viewer); return TRUE;
        case GDK_KEY_Left: case GDK_KEY_Page_Up: navigate(viewer, -1); return TRUE;
        case GDK_KEY_Right: case GDK_KEY_Page_Down: navigate(viewer, 1); return TRUE;
        case GDK_KEY_plus: case GDK_KEY_equal: set_zoom(viewer, 1.25); return TRUE;
        case GDK_KEY_minus: set_zoom(viewer, 0.8); return TRUE;
        case GDK_KEY_0: viewer->fit = true; viewer->zoom = 1.0; gtk_widget_queue_draw(viewer->drawing); return TRUE;
        case GDK_KEY_r: rotate_image(viewer, true); return TRUE;
        case GDK_KEY_R: rotate_image(viewer, false); return TRUE;
        case GDK_KEY_h: flip_image(viewer, true); return TRUE;
        case GDK_KEY_v: flip_image(viewer, false); return TRUE;
        case GDK_KEY_c: toggle_crop(viewer); return TRUE;
        case GDK_KEY_Return: case GDK_KEY_KP_Enter: if (viewer->crop_mode) apply_crop(viewer); return TRUE;
        case GDK_KEY_space: toggle_slideshow(viewer); return TRUE;
        case GDK_KEY_i: toggle_info(viewer); return TRUE;
        case GDK_KEY_F5: toggle_slideshow(viewer); return TRUE;
        case GDK_KEY_o: return FALSE;
        case GDK_KEY_s: return FALSE;
        case GDK_KEY_Delete: delete_file(viewer); return TRUE;
        default: return FALSE;
    }
}

GtkWidget* button(const char* label, const char* tooltip, GCallback callback, Viewer* viewer) {
    GtkWidget* result = gtk_button_new_with_label(label);
    gtk_widget_set_tooltip_text(result, tooltip);
    g_signal_connect(result, "clicked", callback, viewer);
    return result;
}

void on_open(GtkWidget*, gpointer data) { open_file(static_cast<Viewer*>(data)); }
void on_folder(GtkWidget*, gpointer data) { open_directory(static_cast<Viewer*>(data)); }
void on_previous(GtkWidget*, gpointer data) { navigate(static_cast<Viewer*>(data), -1); }
void on_next(GtkWidget*, gpointer data) { navigate(static_cast<Viewer*>(data), 1); }
void on_rotate_left(GtkWidget*, gpointer data) { rotate_image(static_cast<Viewer*>(data), false); }
void on_rotate_right(GtkWidget*, gpointer data) { rotate_image(static_cast<Viewer*>(data), true); }
void on_flip_h(GtkWidget*, gpointer data) { flip_image(static_cast<Viewer*>(data), true); }
void on_flip_v(GtkWidget*, gpointer data) { flip_image(static_cast<Viewer*>(data), false); }
void on_zoom_in(GtkWidget*, gpointer data) { set_zoom(static_cast<Viewer*>(data), 1.25); }
void on_zoom_out(GtkWidget*, gpointer data) { set_zoom(static_cast<Viewer*>(data), 0.8); }
void on_fit(GtkWidget*, gpointer data) {
    auto* viewer = static_cast<Viewer*>(data);
    viewer->fit = true; viewer->zoom = 1.0; gtk_widget_queue_draw(viewer->drawing);
}
void on_slideshow(GtkWidget*, gpointer data) { toggle_slideshow(static_cast<Viewer*>(data)); }
void on_crop(GtkWidget*, gpointer data) { toggle_crop(static_cast<Viewer*>(data)); }
void on_save(GtkWidget*, gpointer data) { save_as(static_cast<Viewer*>(data)); }
void on_copy(GtkWidget*, gpointer data) { copy_to_clipboard(static_cast<Viewer*>(data)); }
void on_wallpaper(GtkWidget*, gpointer data) { set_wallpaper(static_cast<Viewer*>(data)); }
void on_rename(GtkWidget*, gpointer data) { rename_file(static_cast<Viewer*>(data)); }
void on_delete(GtkWidget*, gpointer data) { delete_file(static_cast<Viewer*>(data)); }
void on_info(GtkWidget*, gpointer data) { toggle_info(static_cast<Viewer*>(data)); }
void on_fullscreen(GtkWidget*, gpointer data) { toggle_fullscreen(static_cast<Viewer*>(data)); }

GtkWidget* menu_item(GtkWidget* menu, const char* label, GCallback callback, Viewer* viewer) {
    GtkWidget* item = gtk_menu_item_new_with_label(label);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    if (callback) g_signal_connect(item, "activate", callback, viewer);
    return item;
}

void add_menu(Viewer* viewer, GtkWidget* bar, const char* title,
              const std::vector<std::pair<const char*, GCallback>>& actions) {
    GtkWidget* root = gtk_menu_item_new_with_label(title);
    GtkWidget* menu = gtk_menu_new();
    for (const auto& action : actions) menu_item(menu, action.first, action.second, viewer);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(root), menu);
    gtk_menu_shell_append(GTK_MENU_SHELL(bar), root);
}

GtkWidget* create_window(Viewer* viewer) {
    viewer->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(viewer->window), "GnuChan Image Viewer");
    gtk_window_set_default_size(GTK_WINDOW(viewer->window), 1100, 760);
    g_signal_connect(viewer->window, "destroy", G_CALLBACK(gtk_main_quit), nullptr);
    g_signal_connect(viewer->window, "key-press-event", G_CALLBACK(key_press), viewer);

    GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_container_add(GTK_CONTAINER(viewer->window), root);

    GtkWidget* menu_bar = gtk_menu_bar_new();
    add_menu(viewer, menu_bar, "File", {
        {"Open image…  Ctrl+O", G_CALLBACK(on_open)},
        {"Open directory…", G_CALLBACK(on_folder)},
        {"Save As…  Ctrl+S", G_CALLBACK(on_save)},
        {"Rename…", G_CALLBACK(on_rename)},
        {"Delete…", G_CALLBACK(on_delete)},
        {"Quit", G_CALLBACK(gtk_main_quit)}
    });
    add_menu(viewer, menu_bar, "View", {
        {"Previous image  ←", G_CALLBACK(on_previous)},
        {"Next image  →", G_CALLBACK(on_next)},
        {"Zoom in  +", G_CALLBACK(on_zoom_in)},
        {"Zoom out  −", G_CALLBACK(on_zoom_out)},
        {"Fit to window  0", G_CALLBACK(on_fit)},
        {"Fullscreen  F11", G_CALLBACK(on_fullscreen)},
        {"Toggle metadata  i", G_CALLBACK(on_info)},
        {"Slideshow  Space", G_CALLBACK(on_slideshow)}
    });
    add_menu(viewer, menu_bar, "Edit", {
        {"Rotate clockwise  r", G_CALLBACK(on_rotate_right)},
        {"Rotate counter-clockwise  R", G_CALLBACK(on_rotate_left)},
        {"Flip horizontal  h", G_CALLBACK(on_flip_h)},
        {"Flip vertical  v", G_CALLBACK(on_flip_v)},
        {"Crop selection  c", G_CALLBACK(on_crop)},
        {"Copy to clipboard  Ctrl+C", G_CALLBACK(on_copy)},
        {"Set as wallpaper", G_CALLBACK(on_wallpaper)}
    });
    gtk_box_pack_start(GTK_BOX(root), menu_bar, FALSE, FALSE, 0);

    GtkWidget* toolbar = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    GtkWidget* row1 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
    GtkWidget* row2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
    const struct { const char* label; const char* tip; GCallback callback; } first[] = {
        {"Open", "Open image (Ctrl+O)", G_CALLBACK(on_open)},
        {"Folder", "Open image directory", G_CALLBACK(on_folder)},
        {"‹", "Previous image", G_CALLBACK(on_previous)},
        {"›", "Next image", G_CALLBACK(on_next)},
        {"Fit", "Fit image to window (0)", G_CALLBACK(on_fit)},
        {"−", "Zoom out (-)", G_CALLBACK(on_zoom_out)},
        {"+", "Zoom in (+)", G_CALLBACK(on_zoom_in)},
        {"Slideshow", "Start or stop slideshow (Space)", G_CALLBACK(on_slideshow)},
        {"Info", "Toggle image metadata (i)", G_CALLBACK(on_info)},
        {"Fullscreen", "Toggle fullscreen (F11)", G_CALLBACK(on_fullscreen)}
    };
    for (const auto& entry : first)
        gtk_box_pack_start(GTK_BOX(row1), button(entry.label, entry.tip, entry.callback, viewer), FALSE, FALSE, 0);
    const struct { const char* label; const char* tip; GCallback callback; } second[] = {
        {"Rotate L", "Rotate counter-clockwise (Shift+R)", G_CALLBACK(on_rotate_left)},
        {"Rotate R", "Rotate clockwise (R)", G_CALLBACK(on_rotate_right)},
        {"Flip H", "Flip horizontally (H)", G_CALLBACK(on_flip_h)},
        {"Flip V", "Flip vertically (V)", G_CALLBACK(on_flip_v)},
        {"Crop", "Select and apply crop (C, then Enter)", G_CALLBACK(on_crop)},
        {"Save As", "Save edited pixels (Ctrl+S)", G_CALLBACK(on_save)},
        {"Copy", "Copy image to clipboard (Ctrl+C)", G_CALLBACK(on_copy)},
        {"Wallpaper", "Set as desktop wallpaper", G_CALLBACK(on_wallpaper)},
        {"Rename", "Rename current file", G_CALLBACK(on_rename)},
        {"Delete", "Permanently delete current file", G_CALLBACK(on_delete)}
    };
    for (const auto& entry : second)
        gtk_box_pack_start(GTK_BOX(row2), button(entry.label, entry.tip, entry.callback, viewer), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), row1, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), row2, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(root), toolbar, FALSE, FALSE, 0);

    GtkWidget* content = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    viewer->drawing = gtk_drawing_area_new();
    gtk_widget_set_hexpand(viewer->drawing, TRUE);
    gtk_widget_set_vexpand(viewer->drawing, TRUE);
    gtk_widget_add_events(viewer->drawing, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK);
    g_signal_connect(viewer->drawing, "draw", G_CALLBACK(draw_image), viewer);
    g_signal_connect(viewer->drawing, "button-press-event", G_CALLBACK(button_press), viewer);
    g_signal_connect(viewer->drawing, "motion-notify-event", G_CALLBACK(motion), viewer);
    g_signal_connect(viewer->drawing, "button-release-event", G_CALLBACK(button_release), viewer);
    gtk_drag_dest_set(viewer->drawing, GTK_DEST_DEFAULT_ALL, nullptr, 0, GDK_ACTION_COPY);
    gtk_drag_dest_add_uri_targets(viewer->drawing);
    g_signal_connect(viewer->drawing, "drag-data-received", G_CALLBACK(dropped), viewer);
    gtk_paned_pack1(GTK_PANED(content), viewer->drawing, TRUE, FALSE);

    viewer->info = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(viewer->info), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(viewer->info, 250, -1);
    viewer->info_text = gtk_label_new(nullptr);
    gtk_label_set_xalign(GTK_LABEL(viewer->info_text), 0.0);
    gtk_label_set_yalign(GTK_LABEL(viewer->info_text), 0.0);
    gtk_label_set_line_wrap(GTK_LABEL(viewer->info_text), TRUE);
    gtk_label_set_selectable(GTK_LABEL(viewer->info_text), TRUE);
    gtk_widget_set_margin_start(viewer->info_text, 10);
    gtk_widget_set_margin_end(viewer->info_text, 10);
    gtk_widget_set_margin_top(viewer->info_text, 10);
    gtk_container_add(GTK_CONTAINER(viewer->info), viewer->info_text);
    gtk_paned_pack2(GTK_PANED(content), viewer->info, FALSE, FALSE);
    gtk_widget_hide(viewer->info);
    gtk_box_pack_start(GTK_BOX(root), content, TRUE, TRUE, 0);

    viewer->status = gtk_label_new("Open an image, or drop one into the window");
    gtk_label_set_xalign(GTK_LABEL(viewer->status), 0.0);
    gtk_widget_set_margin_start(viewer->status, 8);
    gtk_box_pack_start(GTK_BOX(root), viewer->status, FALSE, FALSE, 3);
    return viewer->window;
}

}  // namespace

int main(int argc, char** argv) {
    const bool smoke_test = argc > 1 && std::string(argv[1]) == "--smoke-test";
    gtk_init(&argc, &argv);
    Viewer viewer;
    create_window(&viewer);
    gtk_widget_show_all(viewer.window);
    gtk_widget_hide(viewer.info);
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--smoke-test") continue;
        const fs::path path(argv[i]);
        if (fs::exists(path)) {
            if (fs::is_directory(path)) populate_directory(&viewer, path, {});
            else populate_directory(&viewer, path.parent_path(), path);
            break;
        }
    }
    if (smoke_test) g_timeout_add(50, [](gpointer) -> gboolean { gtk_main_quit(); return G_SOURCE_REMOVE; }, nullptr);
    gtk_main();
    if (viewer.animation_timer) g_source_remove(viewer.animation_timer);
    if (viewer.slideshow_timer) g_source_remove(viewer.slideshow_timer);
    if (viewer.animation_iter) g_object_unref(viewer.animation_iter);
    if (viewer.animation) g_object_unref(viewer.animation);
    if (viewer.frame) g_object_unref(viewer.frame);
    if (viewer.pixbuf) g_object_unref(viewer.pixbuf);
    return 0;
}
