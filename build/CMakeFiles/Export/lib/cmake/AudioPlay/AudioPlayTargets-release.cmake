#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "AudioPlay::audioplay" for configuration "Release"
set_property(TARGET AudioPlay::audioplay APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(AudioPlay::audioplay PROPERTIES
  IMPORTED_LINK_DEPENDENT_LIBRARIES_RELEASE "AudioEngine::audio_engine"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libaudioplay.so.3.6.0"
  IMPORTED_SONAME_RELEASE "libaudioplay.so.3"
  )

list(APPEND _IMPORT_CHECK_TARGETS AudioPlay::audioplay )
list(APPEND _IMPORT_CHECK_FILES_FOR_AudioPlay::audioplay "${_IMPORT_PREFIX}/lib/libaudioplay.so.3.6.0" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
