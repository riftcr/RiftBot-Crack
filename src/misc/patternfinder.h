#pragma once

#include <array>
#include <cassert>
#include <cstddef>
#include <optional>
#include <span>
#include <string_view>

// From CSGOSimple

inline std::vector<char> HexToBytes(const std::string& hex) {
  std::vector<char> res;

  for (auto i = 0u; i < hex.length(); i += 2) {
    std::string byteString = hex.substr(i, 2);
    char byte = (char)strtol(byteString.c_str(), NULL, 16);
    res.push_back(byte);
  }

  return res;
}
inline std::string BytesToString(unsigned char* data, int len) {
  constexpr char hexmap[] = {'0', '1', '2', '3', '4', '5', '6', '7',
                             '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
  std::string res(len * 2, ' ');
  for (int i = 0; i < len; ++i) {
    res[2 * i] = hexmap[(data[i] & 0xF0) >> 4];
    res[2 * i + 1] = hexmap[data[i] & 0x0F];
  }
  return res;
}

/*
 * @brief Scan for a given byte pattern on a module
 *
 * @param module    Base of the module to search
 * @param signature IDA-style byte array pattern
 *
 * @returns Address of the first occurence
 */
inline std::uint8_t* PatternScan(void* module, const char* signature) {
  static auto pattern_to_byte = [](const char* pattern) {
    auto bytes = std::vector<int>{};
    auto start = const_cast<char*>(pattern);
    auto end = const_cast<char*>(pattern) + strlen(pattern);

    for (auto current = start; current < end; ++current) {
      if (*current == '?') {
        ++current;
        if (*current == '?') ++current;
        bytes.push_back(-1);
      } else {
        bytes.push_back(strtoul(current, &current, 16));
      }
    }
    return bytes;
  };

  auto dosHeader = (PIMAGE_DOS_HEADER)module;
  auto ntHeaders = (PIMAGE_NT_HEADERS)((std::uint8_t*)module + dosHeader->e_lfanew);

  auto sizeOfImage = ntHeaders->OptionalHeader.SizeOfImage;
  auto patternBytes = pattern_to_byte(signature);
  auto scanBytes = reinterpret_cast<std::uint8_t*>(module);

  auto s = patternBytes.size();
  auto d = patternBytes.data();

  for (auto i = 0ul; i < sizeOfImage - s; ++i) {
    bool found = true;
    for (auto j = 0ul; j < s; ++j) {
      if (scanBytes[i + j] != d[j] && d[j] != -1) {
        found = false;
        break;
      }
    }
    if (found) {
      return &scanBytes[i];
    }
  }
  return nullptr;
}
