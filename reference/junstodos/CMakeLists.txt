cmake_minimum_required(VERSION 3.20)
project(JunsTodos)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# iOS specific settings
if(IOS)
    set(DEVELOPMENT_TEAM "" CACHE STRING "Apple Development Team ID (required for device builds)")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "15.0" CACHE STRING "Minimum iOS deployment version")
    set(CMAKE_XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2" CACHE STRING "iPhone and iPad")
    set(CMAKE_XCODE_ATTRIBUTE_ONLY_ACTIVE_ARCH "NO")
    set(CMAKE_XCODE_ATTRIBUTE_ENABLE_BITCODE "NO")

    # Set bundle properties for Info.plist
    set(MACOSX_BUNDLE_GUI_IDENTIFIER "com.example.junstodos")
    set(MACOSX_BUNDLE_BUNDLE_NAME "JunsTodos")
    set(MACOSX_BUNDLE_EXECUTABLE_NAME "JunsTodos")
    set(MACOSX_BUNDLE_DISPLAY_NAME "Jun's Todos")

    # Configure Info.plist
    configure_file(
        ${CMAKE_SOURCE_DIR}/Info.plist.in
        ${CMAKE_BINARY_DIR}/Info.plist
        @ONLY
    )
endif()

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)

# AELMDB + negentropy sync stack (see third_party/)
include_directories(
    ${CMAKE_SOURCE_DIR}/third_party
    ${CMAKE_SOURCE_DIR}/third_party/aelmdb
    ${CMAKE_SOURCE_DIR}/third_party/negentropy
    ${CMAKE_SOURCE_DIR}/third_party/apple-compat
)

# Source files
set(SOURCES
    src/sokol.mm
    src/main.cpp
    src/font/text.cpp
    src/font/text_ios.mm
    src/db/store.cpp
    src/db/store_test.cpp
    src/db/aelmdb_smoke.cpp
    src/db/db_smoke_ios.cpp
    src/sync/protocol.cpp
    src/sync/server.cpp
    src/sync/mirror.cpp
    src/sync/sync_test.cpp
    src/sync/net_ios.mm
    src/sync/net_demo.cpp
    src/sync/auth.cpp
    src/sync/auth_test.cpp
    src/sync/watch.cpp
    src/engine/value.cpp
    src/engine/db.cpp
    src/engine/engine_test.cpp
    src/engine/glue.cpp
    src/engine/glue_test.cpp
    src/app/todo_app.cpp
    third_party/monocypher/monocypher.c
    third_party/monocypher/monocypher-ed25519.c
    third_party/aelmdb/mdb.c
    third_party/aelmdb/midl.c
)

# iOS prohibits System V IPC (semget raises SIGSYS); LMDB's Darwin default
# is SysV semaphores, so force POSIX named semaphores instead. Global (not
# per-source) so every CMake generator reliably applies it; only mdb.c reads it.
add_compile_definitions(MDB_USE_POSIX_SEM=1)

# Create executable as bundle for iOS
add_executable(${PROJECT_NAME} MACOSX_BUNDLE ${SOURCES})

# iOS frameworks
if(IOS)
    target_link_libraries(${PROJECT_NAME}
        "-framework Foundation"
        "-framework CoreText"
        "-framework CoreGraphics"
        "-framework Metal"
        "-framework MetalKit"
        "-framework QuartzCore"
        "-framework UIKit"
    )

    # Set bundle properties
    set_target_properties(${PROJECT_NAME} PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_BINARY_DIR}/Info.plist
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.example.junstodos"
        XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Automatic"
        XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "${DEVELOPMENT_TEAM}"
        XCODE_ATTRIBUTE_ENABLE_BITCODE "NO"
    )
endif()
