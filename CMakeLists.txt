# CMakeList.txt : CMake project for lab3d-sdl, include source and define
# project specific logic here.
#
cmake_minimum_required (VERSION 3.16)
project(lab3d-sdl)

set (LAB3D_VERSION_MAJOR 4)
set (LAB3D_VERSION_MINOR 1)
set (LAB3D_VERSION_PATCH 5)

if (APPLE OR WIN32)
    set(KEN_EXE_NAME "Kens-Labyrinth")
else ()
    set(KEN_EXE_NAME "ken")
endif ()

set(MAIN_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/include)
set(MAIN_SOURCE_DIR ${CMAKE_SOURCE_DIR}/src)
set(EXTERNAL_DIR ${CMAKE_SOURCE_DIR}/external)
set(ICONS_DIR ${CMAKE_SOURCE_DIR}/icons)

set(ENABLE_HIRES_TEXTURES ON CACHE BOOL "Enable hires textures? Do not turn on if building from Xcode")

if (APPLE)
  set(CMAKE_OSX_ARCHITECTURES arm64 x86_64)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS deployment version")
  set(CMAKE_C_COMPILER clang)
  set(CMAKE_FIND_FRAMEWORK LAST)
  set(MACOSX_BUNDLE_GUI_IDENTIFIER ${BUNDLE_IDENTIFIER})
  set(MACOSX_BUNDLE_IDENTIFIER ${BUNDLE_IDENTIFIER})
  set(MACOSX_BUNDLE_BUNDLE_NAME ${KEN_EXE_NAME})
  set(MACOSX_INFO_STRING "${LAB3D_VERSION_MAJOR}.${LAB3D_VERSION_MINOR}.${LAB3D_VERSION_PATCH}")
  set(MACOSX_SHORT_VERSION_STRING "${LAB3D_VERSION_MAJOR}.${LAB3D_VERSION_MINOR}.${LAB3D_VERSION_PATCH}")
  set(MACOSX_LONG_VERSION_STRING "${LAB3D_VERSION_MAJOR}.${LAB3D_VERSION_MINOR}.${LAB3D_VERSION_PATCH}")
  set(MACOSX_BUNDLE_VERSION "${LAB3D_VERSION_MAJOR}.${LAB3D_VERSION_MINOR}.${LAB3D_VERSION_PATCH}")
  set(MACOSX_INFO_STRING "Copyright © 2024. All rights reserved.")
  set(MACOSX_BUNDLE_ICON_FILE ken.icns)
  set(APP_ICON_MACOSX ${ICONS_DIR}/ken.icns)
  set_source_files_properties(${APP_ICON_MACOSX} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
endif ()

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

if (WIN32)
  option(KEN_64BIT "Compile for 64 bit target?" ON)

  if (CMAKE_GENERATOR MATCHES "64")
    set(KEN_64BIT ON CACHE BOOL "Compile for 64 bit target?")
  else ()
    set(KEN_64BIT OFF CACHE BOOL "Compile for 64 bit target?")
  endif ()
endif ()

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
target_compile_definitions(${KEN_EXE_NAME} PUBLIC KEN_64BIT)
if (ENABLE_HIRES_TEXTURES)
  target_compile_definitions(${KEN_EXE_NAME} PUBLIC ENABLE_HIRES_TEXTURES)
endif ()

if (WIN32)
  if (KEN_64BIT)
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

  find_library(OPENGL opengl32)
  find_library(GLU glu32)
  find_library(WINMM winmm)

  target_link_libraries(${KEN_EXE_NAME} ${OPENGL})
  target_link_libraries(${KEN_EXE_NAME} ${GLU})
  target_link_libraries(${KEN_EXE_NAME} ${WINMM})
  set_target_properties(${KEN_EXE_NAME} PROPERTIES LINK_FLAGS "/NODEFAULTLIB:library")
elseif (UNIX AND NOT APPLE)
  target_link_libraries(${KEN_EXE_NAME} GL)
  target_link_libraries(${KEN_EXE_NAME} GLU)
  target_link_libraries(${KEN_EXE_NAME} m)
  target_link_libraries(${KEN_EXE_NAME} z)
  target_link_libraries(${KEN_EXE_NAME} SDL2)
  target_link_libraries(${KEN_EXE_NAME} SDL2_image)
  target_compile_options(${KEN_EXE_NAME} PUBLIC -O3 -Wall)
  if (KEN_USE_OSS)
    target_compile_definitions(${KEN_EXE_NAME} PUBLIC USE_OSS)
  endif ()
elseif (APPLE)
  find_package(OpenGL REQUIRED)
  find_package(SDL2 REQUIRED)
  find_library(SDL2 SDL2)
  find_library(MLIB m)
  find_library(SDL2_image SDL2_image)
  find_library(CoreFoundation CoreFoundation)
  find_package(ZLIB REQUIRED)
  target_include_directories(${KEN_EXE_NAME} PUBLIC /opt/X11/include)
  target_link_libraries(${KEN_EXE_NAME} ${OPENGL_LIBRARIES} ${SDL2} ${SDL2_image} ${MLIB} ${ZLIB_LIBRARIES} ${CoreFoundation})
  set_target_properties(${KEN_EXE_NAME} PROPERTIES MACOSX_BUNDLE TRUE RESOURCE ${CMAKE_SOURCE_DIR}/gamedata)
  set(CMAKE_INSTALL_PREFIX "${PROJECT_BINARY_DIR}/${KEN_EXE_NAME}.app/Contents")
  set_target_properties(${KEN_EXE_NAME} PROPERTIES 
  XCODE_ATTRIBUTE_CONFIGURATION_BUILD_DIR ${PROJECT_BINARY_DIR})
  set_target_properties(${KEN_EXE_NAME} PROPERTIES
    PRODUCT_BUNDLE_IDENTIFIER "net.minotaurcreative.Kens-Labyrinth"
)
endif ()

if (MSVC)
  if (KEN_64BIT)
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
  file(COPY ${CMAKE_SOURCE_DIR}/gamedata DESTINATION ${PROJECT_BINARY_DIR})
  file(COPY ${ICONS_DIR}/ken.bmp DESTINATION ${PROJECT_BINARY_DIR})
  file(COPY ${CMAKE_SOURCE_DIR}/ksmmidi.txt DESTINATION ${PROJECT_BINARY_DIR})
endif ()