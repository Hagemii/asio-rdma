#----------------------------------------------------------------
# Generated CMake target import file for configuration "Debug".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "Boost::fiber_numa" for configuration "Debug"
set_property(TARGET Boost::fiber_numa APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(Boost::fiber_numa PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_DEBUG "CXX"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/lib64/libboost_fiber_numa.a"
  )

list(APPEND _cmake_import_check_targets Boost::fiber_numa )
list(APPEND _cmake_import_check_files_for_Boost::fiber_numa "${_IMPORT_PREFIX}/lib64/libboost_fiber_numa.a" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
