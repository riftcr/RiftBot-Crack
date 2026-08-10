file(GLOB_RECURSE SRC CONFIGURE_DEPENDS "*.cc")
add_library(rc SHARED ${SRC})
target_include_directories(rc PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/" "${CMAKE_CURRENT_SOURCE_DIR}/misc")
target_precompile_headers(rc PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/pch.h")
target_link_libraries(rc PRIVATE
  absl::strings absl::flat_hash_map absl::flat_hash_set absl::random_random absl::time absl::status absl::flags_parse absl::log
  phnt::phnt WIL::WIL
  simdjson::simdjson unofficial-sodium::sodium CURL::libcurl magic_enum::magic_enum minhook::minhook
)
