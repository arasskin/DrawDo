cmake_minimum_required(VERSION 3.22)
project(datafriend_demo LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
get_filename_component(REPO "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
set(DATAFRIEND_TESTS OFF CACHE BOOL "" FORCE)
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
  enable_language(OBJCXX)
  set(CMAKE_OBJCXX_STANDARD 20)
  set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
  set(DATAFRIEND_SHARED OFF CACHE BOOL "" FORCE)
endif()
add_subdirectory("${REPO}/datafriend" datafriend)
include(cmake/yrs.cmake)

if(ANDROID)
  add_library(drawdo_sokol SHARED
    src/main.cpp src/render/sokol_backend.cpp src/render/text.cpp
    src/platform/android/runtime.cpp
    src/platform/android/text.cpp src/platform/android/text_input.cpp)
  target_include_directories(drawdo_sokol PRIVATE src "${REPO}/third_party")
  target_compile_definitions(drawdo_sokol PRIVATE SOKOL_GLES3)
  target_compile_options(drawdo_sokol PRIVATE -Wall -Wextra -ffp-contract=off)
  target_link_libraries(drawdo_sokol PRIVATE datafriend::datafriend document_yrs android log EGL GLESv3 jnigraphics)
  target_link_options(drawdo_sokol PRIVATE -Wl,--no-undefined)
elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")
  set(DEVELOPMENT_TEAM "" CACHE STRING "Apple signing team")
  add_executable(drawdo_sokol MACOSX_BUNDLE
    src/main.cpp src/render/sokol_backend.cpp src/render/text.cpp
    src/platform/apple/runtime.mm src/platform/apple/text.mm src/platform/apple/text_input.mm)
  set_source_files_properties(src/main.cpp src/render/sokol_backend.cpp PROPERTIES LANGUAGE OBJCXX)
  target_include_directories(drawdo_sokol PRIVATE src "${REPO}/third_party")
  target_compile_definitions(drawdo_sokol PRIVATE SOKOL_METAL)
  # Xcode does not preserve COMPILE_LANGUAGE generator expressions for a mixed
  # C++/Objective-C++ target; set ARC directly on the Objective-C++ sources.
  set_source_files_properties(src/main.cpp src/render/sokol_backend.cpp
    src/platform/apple/runtime.mm src/platform/apple/text.mm src/platform/apple/text_input.mm
    PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
  target_compile_options(drawdo_sokol PRIVATE -Wall -Wextra -ffp-contract=off)
  target_link_libraries(drawdo_sokol PRIVATE datafriend::datafriend document_yrs
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
  # Model and cache tests do not require a renderer, window system, or phone.
  enable_testing()
  add_executable(document_network_test tests/document_network_test.cpp)
  target_include_directories(document_network_test PRIVATE src)
  target_link_libraries(document_network_test PRIVATE datafriend::datafriend document_yrs)
  foreach(name particle_model task_model text_cache document document_model)
    add_executable(${name}_test tests/${name}_test.cpp)
    target_include_directories(${name}_test PRIVATE src)
    target_compile_options(${name}_test PRIVATE -Wall -Wextra -ffp-contract=off)
    if(NOT name STREQUAL "text_cache")
      target_link_libraries(${name}_test PRIVATE datafriend::datafriend)
    endif()
    add_test(NAME ${name}_test COMMAND ${name}_test)
  endforeach()
  target_link_libraries(document_test PRIVATE document_yrs)
  target_link_libraries(document_model_test PRIVATE document_yrs)
endif()
