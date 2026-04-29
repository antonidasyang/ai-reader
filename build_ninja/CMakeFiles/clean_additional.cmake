# Additional clean files
cmake_minimum_required(VERSION 3.16)

if("${CONFIG}" STREQUAL "" OR "${CONFIG}" STREQUAL "Release")
  file(REMOVE_RECURSE
  "CMakeFiles/ai-reader_autogen.dir/AutogenUsed.txt"
  "CMakeFiles/ai-reader_autogen.dir/ParseCache.txt"
  "_deps/cmark-build/extensions/CMakeFiles/libcmark-gfm-extensions_static_autogen.dir/AutogenUsed.txt"
  "_deps/cmark-build/extensions/CMakeFiles/libcmark-gfm-extensions_static_autogen.dir/ParseCache.txt"
  "_deps/cmark-build/extensions/libcmark-gfm-extensions_static_autogen"
  "_deps/cmark-build/src/CMakeFiles/cmark-gfm_autogen.dir/AutogenUsed.txt"
  "_deps/cmark-build/src/CMakeFiles/cmark-gfm_autogen.dir/ParseCache.txt"
  "_deps/cmark-build/src/CMakeFiles/libcmark-gfm_static_autogen.dir/AutogenUsed.txt"
  "_deps/cmark-build/src/CMakeFiles/libcmark-gfm_static_autogen.dir/ParseCache.txt"
  "_deps/cmark-build/src/cmark-gfm_autogen"
  "_deps/cmark-build/src/libcmark-gfm_static_autogen"
  "_deps/microtex-build/CMakeFiles/LaTeXQtSample_autogen.dir/AutogenUsed.txt"
  "_deps/microtex-build/CMakeFiles/LaTeXQtSample_autogen.dir/ParseCache.txt"
  "_deps/microtex-build/CMakeFiles/LaTeX_autogen.dir/AutogenUsed.txt"
  "_deps/microtex-build/CMakeFiles/LaTeX_autogen.dir/ParseCache.txt"
  "_deps/microtex-build/LaTeXQtSample_autogen"
  "_deps/microtex-build/LaTeX_autogen"
  "_deps/qtkeychain-build/CMakeFiles/qt6keychain_autogen.dir/AutogenUsed.txt"
  "_deps/qtkeychain-build/CMakeFiles/qt6keychain_autogen.dir/ParseCache.txt"
  "_deps/qtkeychain-build/qt6keychain_autogen"
  "_deps/tinyxml2-build/CMakeFiles/tinyxml2_autogen.dir/AutogenUsed.txt"
  "_deps/tinyxml2-build/CMakeFiles/tinyxml2_autogen.dir/ParseCache.txt"
  "_deps/tinyxml2-build/tinyxml2_autogen"
  "ai-reader_autogen"
  )
endif()
