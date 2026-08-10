file(GLOB_RECURSE SRC CONFIGURE_DEPENDS "*.cc")
add_executable(riftcrack-launcher ${SRC} app.manifest)
target_link_options(riftcrack-launcher PRIVATE /MANIFESTUAC:NO)
target_include_directories(riftcrack-launcher PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/" "${CMAKE_CURRENT_SOURCE_DIR}/misc")
target_precompile_headers(riftcrack-launcher PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/pch.h")
target_link_libraries(riftcrack-launcher PRIVATE
  absl::strings absl::flat_hash_map absl::flat_hash_set absl::random_random absl::time absl::status absl::flags_parse absl::log $<LINK_LIBRARY:WHOLE_ARCHIVE,absl::log_flags> absl::log_initialize
  phnt::phnt WIL::WIL CURL::libcurl
)
