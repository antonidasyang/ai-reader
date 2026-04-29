#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "qt6keychain" for configuration "Release"
set_property(TARGET qt6keychain APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(qt6keychain PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libqt6keychain.a"
  )

list(APPEND _cmake_import_check_targets qt6keychain )
list(APPEND _cmake_import_check_files_for_qt6keychain "${_IMPORT_PREFIX}/lib/libqt6keychain.a" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
