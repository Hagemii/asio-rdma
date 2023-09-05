#----------------------------------------------------------------
# Generated CMake target import file for configuration "Debug".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "Boost::locale" for configuration "Debug"
set_property(TARGET Boost::locale APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(Boost::locale PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_DEBUG "CXX"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/lib64/libboost_locale.a"
  )

list(APPEND _cmake_import_check_targets Boost::locale )
list(APPEND _cmake_import_check_files_for_Boost::locale "${_IMPORT_PREFIX}/lib64/libboost_locale.a" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
