cmake_minimum_required(VERSION 3.16)
project(GnuChanImageViewer VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0 gdk-pixbuf-2.0 libwebp)

add_library(image_core src/image_core.cpp)
target_include_directories(image_core PUBLIC src)
target_link_libraries(image_core PUBLIC PkgConfig::GTK3)
target_compile_options(image_core PRIVATE -Wall -Wextra -Wpedantic)

add_executable(gnuchan-image-viewer src/main.cpp)
target_link_libraries(gnuchan-image-viewer PRIVATE image_core PkgConfig::GTK3)
target_compile_options(gnuchan-image-viewer PRIVATE -Wall -Wextra -Wpedantic)

include(CTest)
if(BUILD_TESTING)
    add_executable(image_core_tests tests/test_image_core.cpp)
    target_link_libraries(image_core_tests PRIVATE image_core)
    target_compile_options(image_core_tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME image_core_tests COMMAND image_core_tests)
endif()

install(TARGETS gnuchan-image-viewer RUNTIME DESTINATION bin)
install(FILES data/gnuchan-image-viewer.desktop DESTINATION share/applications)
