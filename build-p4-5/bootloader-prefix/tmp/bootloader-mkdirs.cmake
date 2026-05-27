# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "/Users/eric/.espressif/v5.5.3/esp-idf/components/bootloader/subproject")
  file(MAKE_DIRECTORY "/Users/eric/.espressif/v5.5.3/esp-idf/components/bootloader/subproject")
endif()
file(MAKE_DIRECTORY
  "/Users/eric/workspace/PiscesMoon-alpha-1.2.0/pisces-moon-p4/build-p4-5/bootloader"
  "/Users/eric/workspace/PiscesMoon-alpha-1.2.0/pisces-moon-p4/build-p4-5/bootloader-prefix"
  "/Users/eric/workspace/PiscesMoon-alpha-1.2.0/pisces-moon-p4/build-p4-5/bootloader-prefix/tmp"
  "/Users/eric/workspace/PiscesMoon-alpha-1.2.0/pisces-moon-p4/build-p4-5/bootloader-prefix/src/bootloader-stamp"
  "/Users/eric/workspace/PiscesMoon-alpha-1.2.0/pisces-moon-p4/build-p4-5/bootloader-prefix/src"
  "/Users/eric/workspace/PiscesMoon-alpha-1.2.0/pisces-moon-p4/build-p4-5/bootloader-prefix/src/bootloader-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/Users/eric/workspace/PiscesMoon-alpha-1.2.0/pisces-moon-p4/build-p4-5/bootloader-prefix/src/bootloader-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/Users/eric/workspace/PiscesMoon-alpha-1.2.0/pisces-moon-p4/build-p4-5/bootloader-prefix/src/bootloader-stamp${cfgdir}") # cfgdir has leading slash
endif()
