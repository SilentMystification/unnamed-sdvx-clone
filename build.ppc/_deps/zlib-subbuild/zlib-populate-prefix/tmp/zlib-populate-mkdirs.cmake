# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/src/build.ppc/_deps/zlib-src"
  "/src/build.ppc/_deps/zlib-build"
  "/src/build.ppc/_deps/zlib-subbuild/zlib-populate-prefix"
  "/src/build.ppc/_deps/zlib-subbuild/zlib-populate-prefix/tmp"
  "/src/build.ppc/_deps/zlib-subbuild/zlib-populate-prefix/src/zlib-populate-stamp"
  "/src/build.ppc/_deps/zlib-subbuild/zlib-populate-prefix/src"
  "/src/build.ppc/_deps/zlib-subbuild/zlib-populate-prefix/src/zlib-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/src/build.ppc/_deps/zlib-subbuild/zlib-populate-prefix/src/zlib-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/src/build.ppc/_deps/zlib-subbuild/zlib-populate-prefix/src/zlib-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
