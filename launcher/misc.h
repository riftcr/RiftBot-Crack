#pragma once
// Code authored by Jan Schultke on StackOverflow licensed CC-BY-SA 4.0

// https://stackoverflow.com/a/63411055
constexpr unsigned log2floor(uint64_t x) {
  // implementation using the new C++20 <bit> header
  return x ? 63 - std::countl_zero(x) : 0;
}

constexpr unsigned log10floor(unsigned x) {
  constexpr unsigned char guesses[32] = {0, 0, 0, 0, 1, 1, 1, 2, 2, 2, 3, 3, 3, 3, 4, 4,
                                         4, 5, 5, 5, 6, 6, 6, 6, 7, 7, 7, 8, 8, 8, 9, 9};
  constexpr uint64_t powers[11] = {1,       10,       100,       1000,       10000,      100000,
                                   1000000, 10000000, 100000000, 1000000000, 10000000000};
  unsigned guess = guesses[log2floor(x)];
  return guess + (x >= powers[guess + 1]);
}
template <typename Uint>
constexpr Uint logFloor_naive(Uint val, unsigned base) {
  Uint result = 0;
  while (val /= base) {
    ++result;
  }
  return result;
}

template <typename Uint, size_t BASE>
constexpr std::array<uint8_t, std::numeric_limits<Uint>::digits> makeGuessTable() {
  decltype(makeGuessTable<Uint, BASE>()) result{};
  for (size_t i = 0; i < result.size(); ++i) {
    Uint pow2 = static_cast<Uint>(Uint{1} << i);
    result.data[i] = logFloor_naive(pow2, BASE);
  }
  return result;
}

// The maximum possible exponent for a given base that can still be represented
// by a given integer type.
// Example: maxExp<uint8_t, 10> = 2, because 10^2 is representable by an 8-bit unsigned
// integer but 10^3 isn't.
template <typename Uint, unsigned BASE>
constexpr Uint maxExp = logFloor_naive<Uint>(static_cast<Uint>(~Uint{0u}), BASE);
// the size of the table is maxPow<Uint, BASE> + 2 because we need to store the maximum power
// +1 because we need to contain it, we are dealing with a size, not an index
// +1 again because for narrow integers, we access guess+1
template <typename Uint, size_t BASE>
constexpr std::array<uint64_t, maxExp<Uint, BASE> + 2> makePowerTable() {
  decltype(makePowerTable<Uint, BASE>()) result{};
  uint64_t x = 1;
  for (size_t i = 0; i < result.size(); ++i, x *= BASE) {
    result.data()[i] = x;
  }
  return result;
}

// If our base is a power of 2, we can convert between the
// logarithms of different bases without losing any precision.
constexpr bool isPow2or0(uint64_t val) { return (val & (val - 1)) == 0; }

template <size_t BASE = 10, typename Uint>
constexpr Uint logFloor(Uint val) {
  if constexpr (isPow2or0(BASE)) {
    return log2floor(val) / log2floor(BASE);
  } else {
    constexpr auto guesses = makeGuessTable<Uint, BASE>();
    constexpr auto powers = makePowerTable<Uint, BASE>();

    uint8_t guess = guesses[log2floor(val)];

    // Accessing guess + 1 isn't always safe for 64-bit integers.
    // This is why we need this condition. See below for more details.
    if constexpr (sizeof(Uint) < sizeof(uint64_t) || guesses.back() + 2 < powers.size()) {
      return guess + (val >= powers[guess + 1]);
    } else {
      return guess + (val / BASE >= powers[guess]);
    }
  }
}

// https://stackoverflow.com/a/63511628
inline std::string StringifyFraction(const unsigned num, const unsigned den,
                                     const unsigned precision) {
  constexpr unsigned base = 10;

  // prevent division by zero if necessary
  if (den == 0) {
    return "inf";
  }

  // integral part can be computed using regular division
  std::string result = std::to_string(num / den);

  // perform first step of long division
  // also cancel early if there is no fractional part
  unsigned tmp = num % den;
  if (tmp == 0 || precision == 0) {
    return result;
  }

  // reserve characters to avoid unnecessary re-allocation
  result.reserve(result.size() + precision + 1);

  // fractional part can be computed using long divison
  result += '.';
  for (size_t i = 0; i < precision; ++i) {
    tmp *= base;
    char nextDigit = '0' + static_cast<char>(tmp / den);
    result.push_back(nextDigit);
    tmp %= den;
  }

  return result;
}
// https://stackoverflow.com/a/63512259
// use SFINAE to only allow base 1000 or 1024
template <size_t BASE = 1024, std::enable_if_t<BASE == 1000 || BASE == 1024, int> = 0>
std::string StringifyFileSize(uint64_t size, unsigned precision = 0) {
  static constexpr char FILE_SIZE_UNITS[8][3]{"B", "KB", "MB", "GB", "TB", "PB", "EB", "ZB"};

  // The linked post about computing the integer logarithm
  // explains how to compute this.
  // This is equivalent to making a table: {1, 1000, 1000 * 1000, ...}
  // or {1, 1024, 1024 * 1024, ...}
  static constexpr auto powers = makePowerTable<uint64_t, BASE>();

  unsigned unit = logFloor<BASE>(size);

  // Your numerator is size, your denominator is 1000^unit or 1024^unit.
  std::string result = StringifyFraction(size, powers[unit], precision);
  result.reserve(result.size() + 5);

  // Optional: Space separating number from unit. (usually looks better)
  result.push_back(' ');
  char first = FILE_SIZE_UNITS[unit][0];
  // Optional: Use lower case (kB, mB, etc.) for decimal units
  if constexpr (BASE == 1000) {
    first += 'a' - 'A';
  }
  result.push_back(first);

  // Don't insert anything more in case of single bytes.
  if (unit != 0) {
    if constexpr (BASE == 1024) {
      result.push_back('i');
    }
    result.push_back(FILE_SIZE_UNITS[unit][1]);
  }

  return result;
}
