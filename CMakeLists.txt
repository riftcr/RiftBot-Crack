cmake_minimum_required(VERSION 3.31)
set(VCPKG_TARGET_TRIPLET "x64-win-llvm-lto-static-md")
set(VCPKG_HOST_TRIPLET "${VCPKG_TARGET_TRIPLET}")
project(riftcrack2)

set(CMAKE_CXX_STANDARD 26)

if (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
  message(STATUS "Detected MSVC")
  add_compile_options(/ARCH:AVX2)
elseif (CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC")
  message(STATUS "Detected Clang-CL")
  add_compile_options(-mavx2 -mlzcnt)
elseif (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  message(STATUS "Detected Clang")
  add_compile_options(-mavx2 -mlzcnt)
endif()

add_compile_definitions(
  NOMINMAX
  WIN32_LEAN_AND_MEAN
  VC_EXTRALEAN
  PHNT_VERSION=PHNT_WINDOWS_11_24H2
)

if (CMAKE_BUILD_TYPE STREQUAL "Release" OR
    CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
  add_compile_definitions(NDEBUG)
endif()

set(WIL_BUILD_PACKAGING OFF)
set(WIL_BUILD_TESTS OFF)
add_subdirectory(third_party/phnt/)
add_subdirectory(third_party/wil/)
find_package(absl CONFIG REQUIRED)
find_package(simdjson CONFIG REQUIRED)
find_package(unofficial-sodium CONFIG REQUIRED)
find_package(CURL REQUIRED)
find_package(magic_enum CONFIG REQUIRED)
find_package(minhook CONFIG REQUIRED)

add_subdirectory(src/)
add_subdirectory(launcher/)
