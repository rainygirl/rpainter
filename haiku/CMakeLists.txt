cmake_minimum_required(VERSION 3.16)
project(RPainter VERSION 1.0.0 LANGUAGES CXX)

# Native Haiku build: Be API only (Interface, Storage and Translation kits). No Qt.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# The editing engine is shared with the macOS build and has no toolkit dependency.
set(ENGINE ${CMAKE_CURRENT_SOURCE_DIR}/../engine)
add_library(rpainter_engine STATIC ${ENGINE}/raster.cpp ${ENGINE}/doc.cpp ${ENGINE}/tools.cpp ${ENGINE}/i18n.cpp)
target_include_directories(rpainter_engine PUBLIC ${ENGINE})
add_executable(rpainter_engine_test ${ENGINE}/tests/engine_test.cpp)
target_link_libraries(rpainter_engine_test PRIVATE rpainter_engine)

if(HAIKU)
  add_executable(RPainter src/app.cpp)
  target_link_libraries(RPainter PRIVATE rpainter_engine be translation tracker)
endif()
