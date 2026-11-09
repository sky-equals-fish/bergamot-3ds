# SPDX-License-Identifier: GPL-3.0-or-later
# Bergamot 3DS player: a small iOS and Mac app around Bergamot::ThreeDS.
# Configure with the same CTR_PLATFORM / toolchain flags as systems/3ds.
cmake_minimum_required(VERSION 3.25)
project(Bergamot3DSPlayer VERSION 0.1.0 LANGUAGES CXX OBJCXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../systems/3ds" bergamot)
set(BG_ROOT "${CMAKE_CURRENT_LIST_DIR}/..")
set(BG_CORE "${BG_ROOT}/build/${CTR_PLATFORM}/bin/Release/azahar_libretro.dylib")
set(BG_LICENSES "${BG_ROOT}/LICENSE" "${BG_ROOT}/NOTICE.txt" "${BG_ROOT}/THIRD_PARTY_NOTICES.txt")

if(CTR_PLATFORM MATCHES "^ios")
    add_executable(Bergamot3DS MACOSX_BUNDLE ios_player.mm)
    target_include_directories(Bergamot3DS PRIVATE "${BG_ROOT}/ios")
    target_link_libraries(Bergamot3DS PRIVATE Bergamot::ThreeDSMetal
        "-framework UIKit" "-framework GameController" "-framework AVFoundation" "-framework UniformTypeIdentifiers")
    # The Bergamot app's 3DS control art, default palette only.
    file(GLOB BG_ART "${BG_ROOT}/ios/PixelArt/exports/default-*.png")
    list(FILTER BG_ART INCLUDE REGEX "default-(control-(a|b|x|y|l|r|zl|zr|home|select|start)|dpad|circle|cstick|skin-bezel|skin-phone-details|background|library-row|field|panel|font|icon-3ds|segment)-[0-9]+\\.png$")
    file(GLOB BG_ICONS "${CMAKE_CURRENT_LIST_DIR}/Icons/AppIcon*.png")
    target_sources(Bergamot3DS PRIVATE ${BG_ART} ${BG_ICONS} "${CMAKE_CURRENT_LIST_DIR}/Icons/fuitlet.png" ${BG_LICENSES})
    set_source_files_properties(${BG_ART} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/PixelArt")
    set_source_files_properties(${BG_ICONS} "${CMAKE_CURRENT_LIST_DIR}/Icons/fuitlet.png" PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
    set_source_files_properties(${BG_LICENSES} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/Licenses")
    set_target_properties(Bergamot3DS PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_LIST_DIR}/iOSInfo.plist"
        BUILD_RPATH "@executable_path/Frameworks" INSTALL_RPATH "@executable_path/Frameworks")
    add_custom_command(TARGET Bergamot3DS POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:Bergamot3DS>/Frameworks"
        COMMAND ${CMAKE_COMMAND} -E copy "${BG_CORE}" "$<TARGET_BUNDLE_DIR:Bergamot3DS>/Frameworks/azahar_libretro.dylib")
else()
    add_executable(Bergamot3DS MACOSX_BUNDLE mac_player.mm)
    target_compile_options(Bergamot3DS PRIVATE -fobjc-arc)
    target_link_libraries(Bergamot3DS PRIVATE Bergamot::ThreeDSMetal
        "-framework AppKit" "-framework GameController" "-framework AVFoundation" "-framework UniformTypeIdentifiers")
    set(BG_ICNS "${CMAKE_CURRENT_LIST_DIR}/Icons/Bergamot3DS.icns" "${CMAKE_CURRENT_LIST_DIR}/Icons/fuitlet.png")
    target_sources(Bergamot3DS PRIVATE ${BG_ICNS} ${BG_LICENSES})
    set_source_files_properties(${BG_ICNS} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
    set_source_files_properties(${BG_LICENSES} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/Licenses")
    set_target_properties(Bergamot3DS PROPERTIES OUTPUT_NAME "Bergamot 3DS"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_LIST_DIR}/MacInfo.plist"
        BUILD_RPATH "@executable_path/../Frameworks" INSTALL_RPATH "@executable_path/../Frameworks")
    add_custom_command(TARGET Bergamot3DS POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:Bergamot3DS>/Contents/Frameworks"
        COMMAND ${CMAKE_COMMAND} -E copy "${BG_CORE}" "$<TARGET_BUNDLE_DIR:Bergamot3DS>/Contents/Frameworks/azahar_libretro.dylib")
endif()
target_compile_options(Bergamot3DS PRIVATE -fobjc-arc -Wall -Wextra)
