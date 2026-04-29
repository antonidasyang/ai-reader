# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/home/yangxc/codes/ai-reader/build_ninja/_deps/microtex-src"
  "/home/yangxc/codes/ai-reader/build_ninja/_deps/microtex-build"
  "/home/yangxc/codes/ai-reader/build_ninja/_deps/microtex-subbuild/microtex-populate-prefix"
  "/home/yangxc/codes/ai-reader/build_ninja/_deps/microtex-subbuild/microtex-populate-prefix/tmp"
  "/home/yangxc/codes/ai-reader/build_ninja/_deps/microtex-subbuild/microtex-populate-prefix/src/microtex-populate-stamp"
  "/home/yangxc/codes/ai-reader/build_ninja/_deps/microtex-subbuild/microtex-populate-prefix/src"
  "/home/yangxc/codes/ai-reader/build_ninja/_deps/microtex-subbuild/microtex-populate-prefix/src/microtex-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/yangxc/codes/ai-reader/build_ninja/_deps/microtex-subbuild/microtex-populate-prefix/src/microtex-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/home/yangxc/codes/ai-reader/build_ninja/_deps/microtex-subbuild/microtex-populate-prefix/src/microtex-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
