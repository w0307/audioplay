#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "AudioEngine::audio_engine" for configuration "Release"
set_property(TARGET AudioEngine::audio_engine APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(AudioEngine::audio_engine PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libaudio_engine.so.1.1.0"
  IMPORTED_SONAME_RELEASE "libaudio_engine.so.1"
  )

list(APPEND _IMPORT_CHECK_TARGETS AudioEngine::audio_engine )
list(APPEND _IMPORT_CHECK_FILES_FOR_AudioEngine::audio_engine "${_IMPORT_PREFIX}/lib/libaudio_engine.so.1.1.0" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
