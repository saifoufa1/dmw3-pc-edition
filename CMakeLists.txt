cmake_minimum_required(VERSION 3.20)
project(dmw3_pc_edition VERSION 0.0.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(/W4 /permissive- /utf-8 /D_CRT_SECURE_NO_WARNINGS)
else()
  add_compile_options(-Wall -Wextra)
endif()

# --- Format library: pure C++, no platform code. Shared by game + tools. ---
add_library(dw3formats STATIC
  src/core/bytes.cpp
  src/formats/image.cpp
  src/formats/tim.cpp
  src/formats/rlen.cpp
  src/formats/sprite_pack.cpp
  src/formats/stage_pack.cpp
  src/formats/field_stage.cpp
  src/formats/stage_overlay.cpp
  src/formats/dw3_text.cpp
  src/formats/ui_sheet.cpp
  src/formats/model.cpp
  src/formats/arena.cpp)
target_include_directories(dw3formats PUBLIC src)

# --- Asset checker / dumper (console) ---
add_executable(dw3check tools/dw3check.cpp)
target_link_libraries(dw3check PRIVATE dw3formats)

# --- The game (Windows first; platform layer is isolated in src/platform) ---
if(WIN32)
  add_executable(dmw3 WIN32
    src/platform/win32_main.cpp
    src/render/gl_renderer.cpp
    src/render/model_renderer.cpp
    src/render/camera3d.cpp
    src/game/common.cpp
    src/game/stage_data.cpp
    src/game/field.cpp
    src/game/battle.cpp
    src/game/story_text.cpp
    src/game/dialogue.cpp
    src/game/story.cpp
    src/ui/ui_assets.cpp
    src/ui/ui_draw.cpp
    src/ui/battle_hud.cpp
    src/ui/result_screen.cpp
    src/ui/ui_preview.cpp
    src/ui/pack_art.cpp
    src/game/account.cpp
    src/game/starter_sets.cpp
    src/game/app.cpp
    src/game/game.cpp)
  target_link_libraries(dmw3 PRIVATE dw3formats opengl32 gdi32 user32 shell32)
  target_compile_definitions(dmw3 PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX
    DMW3_VERSION="${PROJECT_VERSION}")
endif()
