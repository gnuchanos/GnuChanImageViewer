# GnuChan Image Viewer

A lightweight Linux desktop image viewer built with **GTK 3** and **GdkPixbuf**. It uses the image loaders installed on the host instead of pretending to support formats it cannot decode.

## Features

- View JPEG/JPG, PNG, animated GIF, BMP and WebP; TIFF and SVG depend on their installed GdkPixbuf loaders. SVG decoding normally comes from the `librsvg` loader.
- Browse the current directory, step through images, open a directory, drag and drop files, and run a three-second slideshow.
- Fit-to-window, zoom, fullscreen, rotate, horizontal/vertical flip, and crop by dragging a region.
- Save edited pixels with the installed GdkPixbuf encoders, copy the displayed image to the clipboard, or set a GNOME wallpaper using `gsettings` (or `feh` on other desktops).
- View dimensions, file size, detected format, transparency, animation state, common JPEG EXIF fields, and detected embedded ICC/sRGB profile markers. Transparency is shown over a checkerboard.
- Rename and delete files, with confirmation before permanent deletion.
- Keyboard shortcuts: `←`/`→` previous/next, `+`/`-` zoom, `0` fit, `F11` fullscreen, `r`/`R` rotate, `h`/`v` flip, `c` then drag and `Enter` to crop, `Space` slideshow, `i` metadata, `Ctrl+O` open, `Ctrl+S` save, `Ctrl+C` copy, `Delete` delete, `Esc` cancel crop/leave fullscreen.

## Honest format support

The application queries GdkPixbuf's active decoders at runtime and adds its direct libwebp decoder. Unsupported extensions are excluded from directory navigation and produce an explicit “Image format unavailable” message with the actual installed extensions; a corrupt or unsupported file is never silently shown as a blank/successful image. TIFF and SVG depend on the corresponding loader being installed. Installing GdkPixbuf loader packages (for example `librsvg` for SVG) enables those formats without changing the app.

Saving is limited to formats with an installed GdkPixbuf encoder. GdkPixbuf saves the currently displayed frame of an animated image as a still image; it does not rewrite a multi-frame animation. Export does not preserve embedded ICC profiles. The viewer reports common embedded profile markers but relies on the system decoder for display color handling; it does not claim a full color-managed proofing workflow. EXIF reporting covers common JPEG make/model/date/orientation values; it does not edit metadata.

## Build dependencies

Debian/Ubuntu packages:

```sh
sudo apt install build-essential pkg-config libgtk-3-dev libgdk-pixbuf-2.0-dev libwebp-dev xvfb
```

WebP uses libwebp directly and is available independently of a GdkPixbuf WebP plugin. Optional system GdkPixbuf loader packages provide TIFF and SVG (such as `librsvg2-common` for SVG).

## Build and test

```sh
make
make test
```

`make test` compiles the C++17 application and core tests, runs the test suite, then opens and closes the GTK application under Xvfb as a GUI smoke test. CMake users may also configure with `cmake -S . -B build && cmake --build build && ctest --test-dir build --output-on-failure`. To launch:

```sh
build/gnuchan-image-viewer [image-or-directory ...]
```

To install into the configured system prefix:

```sh
sudo make install
```
