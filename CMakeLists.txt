# CMakeList.txt : CMake project for lab3d-sdl
#
cmake_minimum_required (VERSION 3.26)

include("${CMAKE_SOURCE_DIR}/version.cmake")

if (APPLE OR WIN32)
    set(KEN_EXE_NAME "Kens-Labyrinth")
else ()
    set(KEN_EXE_NAME "ken")
endif ()

set(MAIN_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/include)
set(MAIN_SOURCE_DIR ${CMAKE_SOURCE_DIR}/src)
set(EXTERNAL_DIR ${CMAKE_SOURCE_DIR}/external)
set(ICONS_DIR ${CMAKE_SOURCE_DIR}/icons)

if (WIN32)
  set(DIST_DIR ${CMAKE_SOURCE_DIR}/dist/windows)
elseif (UNIX AND NOT APPLE)
  set(DIST_DIR ${CMAKE_SOURCE_DIR}/dist/linux)
elseif (APPLE)
  set(DIST_DIR ${CMAKE_SOURCE_DIR}/dist/macOS)
endif ()
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${DIST_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${DIST_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${DIST_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${DIST_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL ${DIST_DIR})

set(ENABLE_HIRES_TEXTURES ON CACHE BOOL "Enable hires textures? Do not turn on if building from Xcode")

if (APPLE)
  set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "12.4" CACHE STRING "Minimum macOS deployment version")
  set(CMAKE_C_COMPILER clang)
  set(CMAKE_FIND_FRAMEWORK LAST)
  set(MACOSX_BUNDLE_GUI_IDENTIFIER "net.minotaurcreative.${KEN_EXE_NAME}")
  set(MACOSX_BUNDLE_NAME ${KEN_EXE_NAME})
  set(MACOSX_INFO_STRING "${LAB3D_VERSION_MAJOR}.${LAB3D_VERSION_MINOR}.${LAB3D_VERSION_PATCH}")
  set(MACOSX_SHORT_VERSION_STRING "${LAB3D_VERSION_MAJOR}.${LAB3D_VERSION_MINOR}.${LAB3D_VERSION_PATCH}")
  set(MACOSX_LONG_VERSION_STRING "${LAB3D_VERSION_MAJOR}.${LAB3D_VERSION_MINOR}.${LAB3D_VERSION_PATCH}")
  set(MACOSX_BUNDLE_VERSION "${LAB3D_VERSION_MAJOR}.${LAB3D_VERSION_MINOR}.${LAB3D_VERSION_PATCH}")
  set(MACOSX_INFO_STRING "Copyright © 2025. All rights reserved.")
  set(MACOSX_BUNDLE_ICON_FILE ken.icns)
  set(CMAKE_XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "2W56SCHJ6N" CACHE STRING "Apple Developer Team ID")
  set(CMAKE_XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "Developer ID Application" CACHE STRING "Apple Developer Code Sign Identity")
  set(CMAKE_XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Manual")
  set(APP_ICON_MACOSX ${ICONS_DIR}/ken.icns)
  set_source_files_properties(${APP_ICON_MACOSX} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
endif ()

project(lab3d-sdl)

configure_file (
  "${CMAKE_SOURCE_DIR}/buildconfig.h.in"
  "${MAIN_INCLUDE_DIR}/buildconfig.h"
  )

include_directories("${MAIN_INCLUDE_DIR}")
include_directories("${MAIN_SOURCE_DIR}")
include_directories("${EXTERNAL_DIR}/SDL2/include")
include_directories("${EXTERNAL_DIR}/SDL2_image/include")
include_directories("${EXTERNAL_DIR}/zlib/include")

file(GLOB LAB3D_PROJECT_INCLUDES
  ${MAIN_INCLUDE_DIR}/*.h
)

file(GLOB LAB3D_PROJECT_SOURCES
  ${MAIN_SOURCE_DIR}/adlibemu.c
  ${MAIN_SOURCE_DIR}/demo.c
  ${MAIN_SOURCE_DIR}/graphx.c
  ${MAIN_SOURCE_DIR}/init.c
  ${MAIN_SOURCE_DIR}/lab3d.c
  ${MAIN_SOURCE_DIR}/launcher.c
  ${MAIN_SOURCE_DIR}/oldlab3d.c
  ${MAIN_SOURCE_DIR}/setup.c
  ${MAIN_SOURCE_DIR}/subs.c
)

if (UNIX AND (NOT APPLE))
  set(CMAKE_INSTALL_RPATH ${CMAKE_SOURCE_DIR})
  set(KEN_USE_OSS ON CACHE BOOL "Use OSS for General MIDI?")
endif ()

if (APPLE)
  file(GLOB GAME_DATA1_0 ${CMAKE_SOURCE_DIR}/gamedata/Ken1.0/*)
  file(GLOB GAME_DATA1_1 ${CMAKE_SOURCE_DIR}/gamedata/Ken1.1/*)
  file(GLOB GAME_DATA2_0 ${CMAKE_SOURCE_DIR}/gamedata/Ken2.0/*)
  file(GLOB GAME_DATA2_0_HIRES ${CMAKE_SOURCE_DIR}/gamedata/Ken2.0/hires/*)
  file(GLOB GAME_DATA2_1 ${CMAKE_SOURCE_DIR}/gamedata/Ken2.1/*)
  file(GLOB GAME_DATA2_1_HIRES ${CMAKE_SOURCE_DIR}/gamedata/Ken2.1/hires/*)
  set(RESOURCE_FILES
  ${ICONS_DIR}/ken.bmp
  ${CMAKE_SOURCE_DIR}/ksmmidi.txt
  )
  set_source_files_properties(
    ${RESOURCE_FILES}
    PROPERTIES
    MACOSX_PACKAGE_LOCATION Resources/kenfiles
  )
  set_source_files_properties(
      ${GAME_DATA1_0}
      PROPERTIES
      MACOSX_PACKAGE_LOCATION Resources/kenfiles/gamedata/Ken1.0
  )
  set_source_files_properties(
      ${GAME_DATA1_1}
      PROPERTIES
      MACOSX_PACKAGE_LOCATION Resources/kenfiles/gamedata/Ken1.1
  )
  set_source_files_properties(
      ${GAME_DATA2_0}
      PROPERTIES
      MACOSX_PACKAGE_LOCATION Resources/kenfiles/gamedata/Ken2.0
  )
  set_source_files_properties(
      ${GAME_DATA2_0_HIRES}
      PROPERTIES
      MACOSX_PACKAGE_LOCATION Resources/kenfiles/gamedata/Ken2.0/hires
  )
  set_source_files_properties(
      ${GAME_DATA2_1}
      PROPERTIES
      MACOSX_PACKAGE_LOCATION Resources/kenfiles/gamedata/Ken2.1
  )
  set_source_files_properties(
      ${GAME_DATA2_1_HIRES}
      PROPERTIES
      MACOSX_PACKAGE_LOCATION Resources/kenfiles/gamedata/Ken2.1/hires
  )
  add_executable (${KEN_EXE_NAME} MACOSX_BUNDLE ${LAB3D_PROJECT_INCLUDES} ${LAB3D_PROJECT_SOURCES} ${APP_ICON_MACOSX} ${ICONS_DIR}/ken.rc ${RESOURCE_FILES} ${GAME_DATA1_0} ${GAME_DATA1_1} ${GAME_DATA2_0} ${GAME_DATA2_1} ${GAME_DATA2_0_HIRES} ${GAME_DATA2_1_HIRES})
else ()
  add_executable (${KEN_EXE_NAME} ${LAB3D_PROJECT_INCLUDES} ${LAB3D_PROJECT_SOURCES} ${ICONS_DIR}/ken.rc)
endif()

set_target_properties(${KEN_EXE_NAME} PROPERTIES LINKER_LANGUAGE C)
if (ENABLE_HIRES_TEXTURES)
  target_compile_definitions(${KEN_EXE_NAME} PUBLIC ENABLE_HIRES_TEXTURES)
endif ()

if (WIN32)
  if (CMAKE_GENERATOR_PLATFORM STREQUAL "ARM64")
    target_link_libraries(${KEN_EXE_NAME} ${EXTERNAL_DIR}/SDL2/lib/ARM64/SDL2.lib)
    target_link_libraries(${KEN_EXE_NAME} ${EXTERNAL_DIR}/SDL2/lib/ARM64/SDL2main.lib)
    target_link_libraries(${KEN_EXE_NAME} ${EXTERNAL_DIR}/SDL2_image/lib/ARM64/SDL2_image.lib)
    target_link_libraries(${KEN_EXE_NAME} ${EXTERNAL_DIR}/zlib/lib/ARM64/zlibstatic.lib)
  elseif (CMAKE_GENERATOR_PLATFORM STREQUAL "x64")
    target_link_libraries(${KEN_EXE_NAME} ${EXTERNAL_DIR}/SDL2/lib/x64/SDL2.lib)
    target_link_libraries(${KEN_EXE_NAME} ${EXTERNAL_DIR}/SDL2/lib/x64/SDL2main.lib)
    target_link_libraries(${KEN_EXE_NAME} ${EXTERNAL_DIR}/SDL2_image/lib/x64/SDL2_image.lib)
    target_link_libraries(${KEN_EXE_NAME} ${EXTERNAL_DIR}/zlib/lib/x64/zlibstat.lib)
  else ()
    target_link_libraries(${KEN_EXE_NAME} ${EXTERNAL_DIR}/SDL2/lib/x86/SDL2.lib)
    target_link_libraries(${KEN_EXE_NAME} ${EXTERNAL_DIR}/SDL2/lib/x86/SDL2main.lib)
    target_link_libraries(${KEN_EXE_NAME} ${EXTERNAL_DIR}/SDL2_image/lib/x86/SDL2_image.lib)
    target_link_libraries(${KEN_EXE_NAME} ${EXTERNAL_DIR}/zlib/lib/x86/zlib.lib)
  endif ()

  find_package(OpenGL REQUIRED)
  target_link_libraries(${KEN_EXE_NAME} opengl32)
  target_link_libraries(${KEN_EXE_NAME} glu32)
  target_link_libraries(${KEN_EXE_NAME} winmm)
  set_target_properties(${KEN_EXE_NAME} PROPERTIES LINK_FLAGS "/NODEFAULTLIB:library")
elseif (UNIX AND NOT APPLE)
  find_package(SDL2 REQUIRED)
  find_package(SDL2_image REQUIRED)
  target_link_libraries(${KEN_EXE_NAME} GL)
  target_link_libraries(${KEN_EXE_NAME} GLU)
  target_link_libraries(${KEN_EXE_NAME} m)
  target_link_libraries(${KEN_EXE_NAME} z)
  target_link_libraries(${KEN_EXE_NAME} SDL2::SDL2-static)
  target_link_libraries(${KEN_EXE_NAME} SDL2_image::SDL2_image-static)
  target_compile_options(${KEN_EXE_NAME} PUBLIC -O3 -Wall)
  if (KEN_USE_OSS)
    target_compile_definitions(${KEN_EXE_NAME} PUBLIC USE_OSS)
  endif ()
elseif (APPLE)
  include_directories( ${ZLIB_INCLUDE_DIRS} )
  find_package(OpenGL REQUIRED)
  find_library(MLIB m)
  find_library(ZLIB libz.1.dylib)
  find_library(ZLIBSTD libzstd.1.dylib)
  find_library(LZMA liblzma.5.dylib)
  find_library(SDL2 libSDL2-2.0.0.dylib)
  find_library(SDL2_IMAGE libSDL2_image-2.0.0.dylib)
  find_library(TIFF libtiff.6.dylib)
  find_library(LERC libLerc.4.dylib)
  find_library(JPEG libjpeg.8.dylib)
  find_library(DEFLATE libdeflate.0.dylib)
  find_library(WEBPDEMUX libwebpdemux.2.dylib)
  find_library(WEBP libwebp.7.dylib)
  find_library(SHARPYUV libsharpyuv.0.dylib)
  find_library(CoreFoundation CoreFoundation)
  target_include_directories(${KEN_EXE_NAME} PUBLIC /opt/X11/include)
  target_link_libraries(${KEN_EXE_NAME} PRIVATE ${SDL2} ${OPENGL_LIBRARIES} ${TIFF} ${LERC} ${ZLIBSTD} ${LZMA} ${JPEG} ${DEFLATE} ${WEBPDEMUX} ${WEBP} ${SHARPYUV} ${SDL2_IMAGE} ${MLIB} ${CoreFoundation} ${ZLIB})  
  set(CMAKE_INSTALL_PREFIX "${PROJECT_BINARY_DIR}/${KEN_EXE_NAME}.app/Contents")
  message(STATUS "CMAKE_INSTALL_PREFIX: ${ZLIB_LIBRARIES}")
  set_property(TARGET ${KEN_EXE_NAME} PROPERTY XCODE_EMBED_FRAMEWORKS 
    ${SDL2}
    ${SDL2_IMAGE}
    ${ZLIB}
    ${TIFF}
    ${ZLIBSTD}
    ${LZMA}
    ${LERC}
    ${JPEG}
    ${DEFLATE}
    ${WEBPDEMUX}
    ${WEBP}
    ${SHARPYUV}
  )
  add_custom_command(TARGET ${KEN_EXE_NAME} POST_BUILD
    COMMAND install_name_tool
        -change ${SDL2}
        @executable_path/../Frameworks/libSDL2-2.0.0.dylib
        ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${KEN_EXE_NAME}.app/Contents/MacOS/${KEN_EXE_NAME}
 )
  add_custom_command(TARGET ${KEN_EXE_NAME} POST_BUILD
    COMMAND install_name_tool
        -change ${SDL2_IMAGE}
        @executable_path/../Frameworks/libSDL2_image-2.0.0.dylib
        ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${KEN_EXE_NAME}.app/Contents/MacOS/${KEN_EXE_NAME}
  )
  add_custom_command(TARGET ${KEN_EXE_NAME} POST_BUILD
    COMMAND install_name_tool
        -change ${ZLIB}
        @executable_path/../Frameworks/libz.1.dylib
        ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${KEN_EXE_NAME}.app/Contents/MacOS/${KEN_EXE_NAME}
  )
  add_custom_command(TARGET ${KEN_EXE_NAME} POST_BUILD
    COMMAND install_name_tool
        -change ${TIFF}
        @executable_path/../Frameworks/libtiff.6.dylib
        ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${KEN_EXE_NAME}.app/Contents/MacOS/${KEN_EXE_NAME}
  )
  add_custom_command(TARGET ${KEN_EXE_NAME} POST_BUILD
    COMMAND install_name_tool
        -change ${ZLIBSTD}
        @executable_path/../Frameworks/libzstd.1.dylib
        ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${KEN_EXE_NAME}.app/Contents/MacOS/${KEN_EXE_NAME}
  )
  add_custom_command(TARGET ${KEN_EXE_NAME} POST_BUILD
    COMMAND install_name_tool
        -change ${LZMA}
        @executable_path/../Frameworks/liblzma.5.dylib
        ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${KEN_EXE_NAME}.app/Contents/MacOS/${KEN_EXE_NAME}
  )
  add_custom_command(TARGET ${KEN_EXE_NAME} POST_BUILD
    COMMAND install_name_tool
        -change ${LERC}
        @executable_path/../Frameworks/libLerc.4.dylib
        ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${KEN_EXE_NAME}.app/Contents/MacOS/${KEN_EXE_NAME}
  )
  add_custom_command(TARGET ${KEN_EXE_NAME} POST_BUILD
    COMMAND install_name_tool
        -change ${JPEG}
        @executable_path/../Frameworks/libjpeg.8.dylib
        ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${KEN_EXE_NAME}.app/Contents/MacOS/${KEN_EXE_NAME}
  )
  add_custom_command(TARGET ${KEN_EXE_NAME} POST_BUILD
    COMMAND install_name_tool
        -change ${DEFLATE}
        @executable_path/../Frameworks/libdeflate.0.dylib
        ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${KEN_EXE_NAME}.app/Contents/MacOS/${KEN_EXE_NAME}
  )
  add_custom_command(TARGET ${KEN_EXE_NAME} POST_BUILD
    COMMAND install_name_tool
        -change ${WEBPDEMUX}
        @executable_path/../Frameworks/libwebpdemux.2.dylib
        ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${KEN_EXE_NAME}.app/Contents/MacOS/${KEN_EXE_NAME}
  )
  add_custom_command(TARGET ${KEN_EXE_NAME} POST_BUILD
    COMMAND install_name_tool
        -change ${WEBP}
        @executable_path/../Frameworks/libwebp.7.dylib
        ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${KEN_EXE_NAME}.app/Contents/MacOS/${KEN_EXE_NAME}
  )
  add_custom_command(TARGET ${KEN_EXE_NAME} POST_BUILD
    COMMAND install_name_tool
        -change ${SHARPYUV}
        @executable_path/../Frameworks/libsharpyuv.0.dylib
        ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${KEN_EXE_NAME}.app/Contents/MacOS/${KEN_EXE_NAME}
  )
  set_target_properties(${KEN_EXE_NAME} PROPERTIES 
    XCODE_ATTRIBUTE_ONLY_ACTIVE_ARCH[variant=Debug] YES
    XCODE_ATTRIBUTE_LD_RUNPATH_SEARCH_PATHS "@executable_path/../Frameworks"
    XCODE_ATTRIBUTE_CONFIGURATION_BUILD_DIR "${CMAKE_SOURCE_DIR}/dist/macOS"
    XCODE_ATTRIBUTE_ENABLE_HARDENED_RUNTIME YES
    XCODE_ATTRIBUTE_OTHER_CODE_SIGN_FLAGS[variant=Release] "--options=runtime --timestamp"
    XCODE_ATTRIBUTE_OTHER_CODE_SIGN_FLAGS[variant=RelWithDebInfo] "--options=runtime --timestamp"
    XCODE_ATTRIBUTE_CODE_SIGN_INJECT_BASE_ENTITLEMENTS[variant=Release] NO
		XCODE_ATTRIBUTE_CODE_SIGN_INJECT_BASE_ENTITLEMENTS[variant=RelWithDebInfo] NO
    XCODE_EMBED_FRAMEWORKS_CODE_SIGN_ON_COPY YES
    XCODE_EMBED_FRAMEWORKS_REMOVE_HEADERS_ON_COPY YES
    BUNDLE TRUE
    MACOSX_BUNDLE TRUE
  )
endif ()

if (MSVC)
  if (CMAKE_GENERATOR_PLATFORM STREQUAL "ARM64")
    add_custom_command(TARGET ${KEN_EXE_NAME} POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different "${EXTERNAL_DIR}/SDL2/lib/ARM64/SDL2.dll" $<TARGET_FILE_DIR:${KEN_EXE_NAME}>)
    add_custom_command(TARGET ${KEN_EXE_NAME} POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different "${EXTERNAL_DIR}/SDL2_image/lib/ARM64/SDL2_image.dll" $<TARGET_FILE_DIR:${KEN_EXE_NAME}>)
    add_custom_command(TARGET ${KEN_EXE_NAME} POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different "${EXTERNAL_DIR}/SDL2_image/lib/ARM64/tiff.dll" $<TARGET_FILE_DIR:${KEN_EXE_NAME}>)
    add_custom_command(TARGET ${KEN_EXE_NAME} POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different "${EXTERNAL_DIR}/SDL2_image/lib/ARM64/webp.dll" $<TARGET_FILE_DIR:${KEN_EXE_NAME}>)
  elseif (CMAKE_GENERATOR_PLATFORM STREQUAL "x64")
    add_custom_command(TARGET ${KEN_EXE_NAME} POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different "${EXTERNAL_DIR}/SDL2/lib/x64/SDL2.dll" $<TARGET_FILE_DIR:${KEN_EXE_NAME}>)
    add_custom_command(TARGET ${KEN_EXE_NAME} POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different "${EXTERNAL_DIR}/SDL2_image/lib/x64/SDL2_image.dll" $<TARGET_FILE_DIR:${KEN_EXE_NAME}>)
    add_custom_command(TARGET ${KEN_EXE_NAME} POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different "${EXTERNAL_DIR}/SDL2_image/lib/x64/libpng16-16.dll" $<TARGET_FILE_DIR:${KEN_EXE_NAME}>)
    add_custom_command(TARGET ${KEN_EXE_NAME} POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different "${EXTERNAL_DIR}/SDL2_image/lib/x64/libjpeg-9.dll" $<TARGET_FILE_DIR:${KEN_EXE_NAME}>)
    add_custom_command(TARGET ${KEN_EXE_NAME} POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different "${EXTERNAL_DIR}/SDL2_image/lib/x64/libtiff-5.dll" $<TARGET_FILE_DIR:${KEN_EXE_NAME}>)
    add_custom_command(TARGET ${KEN_EXE_NAME} POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different "${EXTERNAL_DIR}/SDL2_image/lib/x64/libwebp-7.dll" $<TARGET_FILE_DIR:${KEN_EXE_NAME}>)
    add_custom_command(TARGET ${KEN_EXE_NAME} POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different "${EXTERNAL_DIR}/SDL2_image/lib/x64/zlib1.dll" $<TARGET_FILE_DIR:${KEN_EXE_NAME}>)
  else ()
    add_custom_command(TARGET ${KEN_EXE_NAME} POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different "${EXTERNAL_DIR}/SDL2/lib/x86/SDL2.dll" $<TARGET_FILE_DIR:${KEN_EXE_NAME}>)
    add_custom_command(TARGET ${KEN_EXE_NAME} POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different "${EXTERNAL_DIR}/SDL2_image/lib/x86/SDL2_image.dll" $<TARGET_FILE_DIR:${KEN_EXE_NAME}>)
    add_custom_command(TARGET ${KEN_EXE_NAME} POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different "${EXTERNAL_DIR}/SDL2_image/lib/x86/libpng16-16.dll" $<TARGET_FILE_DIR:${KEN_EXE_NAME}>)
    add_custom_command(TARGET ${KEN_EXE_NAME} POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different "${EXTERNAL_DIR}/SDL2_image/lib/x86/libjpeg-9.dll" $<TARGET_FILE_DIR:${KEN_EXE_NAME}>)
    add_custom_command(TARGET ${KEN_EXE_NAME} POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different "${EXTERNAL_DIR}/SDL2_image/lib/x86/libtiff-5.dll" $<TARGET_FILE_DIR:${KEN_EXE_NAME}>)
    add_custom_command(TARGET ${KEN_EXE_NAME} POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different "${EXTERNAL_DIR}/SDL2_image/lib/x86/libwebp-7.dll" $<TARGET_FILE_DIR:${KEN_EXE_NAME}>)
    add_custom_command(TARGET ${KEN_EXE_NAME} POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different "${EXTERNAL_DIR}/SDL2_image/lib/x86/zlib1.dll" $<TARGET_FILE_DIR:${KEN_EXE_NAME}>)
  endif ()
endif ()

if (NOT APPLE) 
  file(COPY ${CMAKE_SOURCE_DIR}/gamedata DESTINATION ${DIST_DIR})
  file(COPY ${ICONS_DIR}/ken.bmp DESTINATION ${DIST_DIR})
  file(COPY ${CMAKE_SOURCE_DIR}/ksmmidi.txt DESTINATION ${DIST_DIR})
endif () 