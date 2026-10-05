cmake_minimum_required(VERSION 3.16)
project(RPainter VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Linux build: Qt Widgets (Qt 6, or Qt 5.12+). No web engine.
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)

add_library(rpainter_core STATIC ../engine/i18n.cpp src/editor.cpp src/canvas.cpp src/icons.cpp src/colorpicker.cpp src/mainwindow.cpp src/platform.cpp)
target_include_directories(rpainter_core PUBLIC src ../engine)
target_link_libraries(rpainter_core PUBLIC Qt${QT_VERSION_MAJOR}::Widgets)

set(CMAKE_AUTORCC ON)
add_executable(RPainter src/main.cpp resources/resources.qrc)
target_link_libraries(RPainter PRIVATE rpainter_core)

# Headless self-test: QT_QPA_PLATFORM=offscreen ./rpainter_selftest [screenshot.png]
add_executable(rpainter_selftest tests/selftest.cpp)
target_link_libraries(rpainter_selftest PRIVATE rpainter_core)

install(TARGETS RPainter RUNTIME DESTINATION bin)
install(FILES r-painter.desktop DESTINATION share/applications)
install(FILES resources/r-painter.png DESTINATION share/icons/hicolor/256x256/apps)
install(FILES resources/r-painter.svg DESTINATION share/icons/hicolor/scalable/apps)
