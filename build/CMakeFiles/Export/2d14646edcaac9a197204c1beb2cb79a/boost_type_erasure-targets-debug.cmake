#----------------------------------------------------------------
# Generated CMake target import file for configuration "Debug".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "Boost::type_erasure" for configuration "Debug"
set_property(TARGET Boost::type_erasure APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(Boost::type_erasure PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_DEBUG "CXX"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/lib64/libboost_type_erasure.a"
  )

list(APPEND _cmake_import_check_targets Boost::type_erasure )
list(APPEND _cmake_import_check_files_for_Boost::type_erasure "${_IMPORT_PREFIX}/lib64/libboost_type_erasure.a" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
