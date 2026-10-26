cmake_minimum_required(VERSION 3.22)
project(datafriend_demo LANGUAGES C)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_C_STANDARD_REQUIRED ON)
get_filename_component(REPO "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
set(DATAFRIEND_TESTS OFF CACHE BOOL "" FORCE)
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
  enable_language(OBJC)
  set(CMAKE_OBJC_STANDARD 99)
  set(CMAKE_OBJC_STANDARD_REQUIRED ON)
  set(DATAFRIEND_SHARED OFF CACHE BOOL "" FORCE)
endif()
add_subdirectory("${REPO}/datafriend" datafriend)
add_subdirectory("${REPO}/text_crdt" text_crdt)
foreach(module text_buffers accessible_ui clipboard haptics)
  add_subdirectory("${REPO}/${module}" ${module})
endforeach()
add_library(app_model STATIC src/base.c src/datafriend_ui.c src/task_model.c src/task_screen.c
  src/particle_model.c src/document_model.c src/document_screen.c
  src/render/text_cache.c src/profiling/clock_probe.c)
target_include_directories(app_model PUBLIC src)
target_link_libraries(app_model PUBLIC datafriend::datafriend text_crdt text_buffers accessible_ui)
target_compile_definitions(app_model PUBLIC _POSIX_C_SOURCE=200809L)
target_compile_options(app_model PRIVATE -Wall -Wextra -Werror -pedantic-errors -ffp-contract=off)
set_target_properties(app_model PROPERTIES POSITION_INDEPENDENT_CODE ON)
set(APP_SOURCES src/main.c src/render/sokol_backend.c src/render/text.c
  src/profiling/task_benchmark.c src/profiling/document_frames.c)


if(ANDROID)
  add_library(drawdo_sokol SHARED
    ${APP_SOURCES}
    src/profiling/presentation.c src/platform/android/runtime.c
    src/platform/android/text.c)
  target_include_directories(drawdo_sokol PRIVATE src "${REPO}/third_party")
  target_compile_definitions(drawdo_sokol PRIVATE SOKOL_GLES3)
  target_compile_options(drawdo_sokol PRIVATE -Wall -Wextra -ffp-contract=off)
  target_link_libraries(drawdo_sokol PRIVATE app_model text_buffers_native accessible_ui_native clipboard haptics m android log EGL GLESv3 jnigraphics)
  target_link_options(drawdo_sokol PRIVATE -Wl,--no-undefined)
elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")
  set(DEVELOPMENT_TEAM "" CACHE STRING "Apple signing team")
  add_executable(drawdo_sokol MACOSX_BUNDLE
    ${APP_SOURCES}
    src/platform/apple/presentation.m src/platform/apple/runtime.m src/platform/apple/text.m)
  set_source_files_properties(src/render/sokol_backend.c PROPERTIES LANGUAGE OBJC)
  target_include_directories(drawdo_sokol PRIVATE src "${REPO}/third_party")
  target_compile_definitions(drawdo_sokol PRIVATE SOKOL_METAL)
  # Keep ARC on the Objective-C platform and Sokol implementation files.
  set_source_files_properties(src/render/sokol_backend.c
    src/platform/apple/presentation.m src/platform/apple/runtime.m src/platform/apple/text.m
    PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
  target_compile_options(drawdo_sokol PRIVATE -Wall -Wextra -ffp-contract=off)
  target_link_libraries(drawdo_sokol PRIVATE app_model text_buffers_native accessible_ui_native clipboard haptics
    "-framework UIKit" "-framework Foundation" "-framework CoreGraphics"
    "-framework CoreText" "-framework QuartzCore" "-framework Metal" "-framework MetalKit" "-framework AudioToolbox")
  set_target_properties(drawdo_sokol PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER "dev.drawdo.sokol"
    MACOSX_BUNDLE_BUNDLE_NAME "Datafriend demo"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/ios/Info.plist.in"
    XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "${DEVELOPMENT_TEAM}"
    XCODE_ATTRIBUTE_CODE_SIGN_STYLE Automatic
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "dev.drawdo.sokol"
    XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2")
else()
  enable_testing()
  foreach(name text editor access task_model particle_model text_cache document document_model)
    add_executable(${name}_test tests/${name}_test.c)
    target_link_libraries(${name}_test PRIVATE app_model)
    target_compile_options(${name}_test PRIVATE -Wall -Wextra -Werror -pedantic-errors -ffp-contract=off)
    add_test(NAME ${name}_test COMMAND ${name}_test)
  endforeach()
  add_executable(document_network_test tests/document_network_test.c)
  target_link_libraries(document_network_test PRIVATE app_model)
  if(NOT APPLE)
    target_link_libraries(app_model PUBLIC m)
  endif()
endif()
