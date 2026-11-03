cmake_minimum_required(VERSION 3.10)
project(AudioEngine VERSION 1.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
find_package(Threads REQUIRED)
option(AUDIO_ENGINE_BUILD_DEMO "Build the generic one-request player" OFF)
set(EKHO_SOURCE_DIR "" CACHE PATH "Patched Ekho v8.9 source directory")
set(EKHO_BUILD_DIR "${EKHO_SOURCE_DIR}" CACHE PATH "Ekho config.h and libekho.a directory")
include(cmake/FindEkho89.cmake)

add_library(audio_engine SHARED src/PlaybackEngine.cpp src/EkhoBackend.cpp src/PulseOutput.cpp)
add_library(AudioEngine::audio_engine ALIAS audio_engine)
target_compile_features(audio_engine PUBLIC cxx_std_11)
target_include_directories(audio_engine PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE src)
target_link_libraries(audio_engine PRIVATE Ekho89 Threads::Threads)
target_compile_options(audio_engine PRIVATE -Wall -Wextra -Wpedantic)
set_target_properties(audio_engine PROPERTIES VERSION ${PROJECT_VERSION} SOVERSION 1 CXX_EXTENSIONS OFF)
if(UNIX AND NOT APPLE)
    set_property(TARGET audio_engine APPEND_STRING PROPERTY LINK_FLAGS " -Wl,--no-undefined")
endif()
if(AUDIO_ENGINE_BUILD_DEMO)
    add_executable(audio_engine_demo examples/play_once.cpp)
    target_link_libraries(audio_engine_demo PRIVATE AudioEngine::audio_engine)
    set_target_properties(audio_engine_demo PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin"
        INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
    install(TARGETS audio_engine_demo RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
install(TARGETS audio_engine EXPORT AudioEngineTargets LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/audio_engine DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT AudioEngineTargets NAMESPACE AudioEngine:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/AudioEngine)
configure_package_config_file(cmake/AudioEngineConfig.cmake.in "${CMAKE_CURRENT_BINARY_DIR}/AudioEngineConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/AudioEngine)
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/AudioEngineConfigVersion.cmake"
    VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/AudioEngineConfig.cmake" "${CMAKE_CURRENT_BINARY_DIR}/AudioEngineConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/AudioEngine)
