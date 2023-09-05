#----------------------------------------------------------------
# Generated CMake target import file for configuration "Debug".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "Boost::wave" for configuration "Debug"
set_property(TARGET Boost::wave APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(Boost::wave PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_DEBUG "CXX"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/lib64/libboost_wave.a"
  )

list(APPEND _cmake_import_check_targets Boost::wave )
list(APPEND _cmake_import_check_files_for_Boost::wave "${_IMPORT_PREFIX}/lib64/libboost_wave.a" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
