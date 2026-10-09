# Yrs is an application dependency; Datafriend remains CRDT-agnostic.
find_program(CARGO_EXECUTABLE cargo HINTS "$ENV{HOME}/.cargo/bin" REQUIRED)
find_program(RUSTC_EXECUTABLE rustc HINTS "$ENV{HOME}/.cargo/bin" REQUIRED)
set(YRS_SOURCE "${REPO}/third_party/yrs")
set(YRS_BUILD "${CMAKE_CURRENT_SOURCE_DIR}/build/yrs")
set(YRS_ENV "CARGO_HOME=${CMAKE_CURRENT_SOURCE_DIR}/build/cargo-home")
if(ANDROID)
  if(NOT ANDROID_ABI STREQUAL "arm64-v8a")
    message(FATAL_ERROR "The document demo currently builds Yrs for arm64-v8a")
  endif()
  set(YRS_TARGET aarch64-linux-android)
  get_filename_component(YRS_LLVM_BIN "${CMAKE_C_COMPILER}" DIRECTORY)
  string(REGEX REPLACE "^android-" "" YRS_ANDROID_API "${ANDROID_PLATFORM}")
  list(APPEND YRS_ENV "CARGO_TARGET_AARCH64_LINUX_ANDROID_LINKER=${YRS_LLVM_BIN}/aarch64-linux-android${YRS_ANDROID_API}-clang")
elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")
  set(YRS_TARGET aarch64-apple-ios)
else()
  execute_process(COMMAND "${RUSTC_EXECUTABLE}" -vV OUTPUT_VARIABLE RUSTC_VERSION COMMAND_ERROR_IS_FATAL ANY)
  string(REGEX MATCH "host: ([^\n]+)" _ "${RUSTC_VERSION}")
  set(YRS_TARGET "${CMAKE_MATCH_1}")
endif()
set(YRS_ARCHIVE "${YRS_BUILD}/${YRS_TARGET}/release/libyrs.a")
file(GLOB_RECURSE YRS_INPUTS CONFIGURE_DEPENDS "${YRS_SOURCE}/*.rs" "${YRS_SOURCE}/*Cargo.toml")
add_custom_command(OUTPUT "${YRS_ARCHIVE}"
  COMMAND "${CMAKE_COMMAND}" -E env ${YRS_ENV} "${CARGO_EXECUTABLE}" build
    --manifest-path "${YRS_SOURCE}/Cargo.toml" --locked --release -p yffi
    --target "${YRS_TARGET}" --target-dir "${YRS_BUILD}" --jobs 4
  DEPENDS ${YRS_INPUTS} "${YRS_SOURCE}/Cargo.lock"
  COMMENT "Building pinned Yrs C API (${YRS_TARGET})" VERBATIM)
add_custom_target(yrs_build DEPENDS "${YRS_ARCHIVE}")
add_library(document_yrs STATIC IMPORTED GLOBAL)
set_target_properties(document_yrs PROPERTIES IMPORTED_LOCATION "${YRS_ARCHIVE}"
  INTERFACE_INCLUDE_DIRECTORIES "${YRS_SOURCE}/include")
add_dependencies(document_yrs yrs_build)
if(UNIX AND NOT APPLE)
  set_property(TARGET document_yrs PROPERTY INTERFACE_LINK_LIBRARIES "${CMAKE_DL_LIBS};m")
endif()
