cmake_minimum_required(VERSION 3.29)

project(CharacterResistanceDisplay LANGUAGES CXX RC)

set(D2RLPLUGIN_BUILD_EXAMPLES OFF CACHE BOOL "Build SDK examples" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "Build SDK tests" FORCE)
set(D2RLPLUGIN_SDK_SOURCE_DIR "" CACHE PATH "Path to the D2RLoader PluginSDK source tree (0.4.0).")
if(NOT D2RLPLUGIN_SDK_SOURCE_DIR)
	if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../PluginSDK/CMakeLists.txt")
		set(D2RLPLUGIN_SDK_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../PluginSDK")
	elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../PluginSDK/SDK-0.4.0/CMakeLists.txt")
		set(D2RLPLUGIN_SDK_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../PluginSDK/SDK-0.4.0")
	endif()
endif()
if(NOT EXISTS "${D2RLPLUGIN_SDK_SOURCE_DIR}/CMakeLists.txt")
	message(FATAL_ERROR "Set D2RLPLUGIN_SDK_SOURCE_DIR to the PluginSDK 0.4.0 source directory.")
endif()
add_subdirectory("${D2RLPLUGIN_SDK_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/plugin-sdk")

add_library(CharacterResistanceDisplay SHARED
	src/plugin.cpp
	src/plugin.rc
)

target_link_libraries(CharacterResistanceDisplay PRIVATE D2RLPlugin::D2RLPlugin)
target_compile_features(CharacterResistanceDisplay PRIVATE cxx_std_20)
if(MSVC)
	target_compile_options(CharacterResistanceDisplay PRIVATE /W4 /permissive- /EHsc /utf-8)
endif()

d2rlplugin_embed_config(
	TARGET CharacterResistanceDisplay
	FILE "${CMAKE_CURRENT_SOURCE_DIR}/character-resistance-display.toml"
)

