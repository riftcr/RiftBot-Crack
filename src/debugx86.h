#pragma once
#include <cstdint>

// clang-format off
typedef union {
  uint64_t flags;
  struct {
    uint64_t  b0  : 1,
              b1  : 1,
              b2  : 1,
              b3  : 1,
                  : 7,
              bld : 1,
              bk  : 1,
              bd  : 1,
              bs  : 1,
              bt  : 1,
              rtm : 1,
                  : 16;
  };
} dr6_t;
enum break_flag_t : uint64_t {
  DR7_BREAK_ON_EXEC = 0,
  DR7_BREAK_ON_WRITE = 1,
  DR7_BREAK_ON_RW = 3,
};
enum data_length_t : uint64_t {
  DR7_LEN_1 = 0,
  DR7_LEN_2 = 1,
  DR7_LEN_4 = 3,
};
typedef union {
  uint64_t flags;
  struct {
    uint64_t dr0_local : 1;
    uint64_t dr0_global : 1;
    uint64_t dr1_local : 1;
    uint64_t dr1_global : 1;
    uint64_t dr2_local : 1;
    uint64_t dr2_global : 1;
    uint64_t dr3_local : 1;
    uint64_t dr3_global : 1;
    uint64_t le : 1;
    uint64_t ge : 1;
    uint64_t reserved_10 : 1;
    uint64_t rtm : 1;
    uint64_t reserved_12 : 1;
    uint64_t gd : 1;
    uint64_t reserved_14_15 : 2;
    break_flag_t dr0_break : 2;
    data_length_t dr0_len : 2;
    break_flag_t dr1_break : 2;
    data_length_t dr1_len : 2;
    break_flag_t dr2_break : 2;
    data_length_t dr2_len : 2;
    break_flag_t dr3_break : 2;
    data_length_t dr3_len : 2;
  };
} dr7_t;

enum X86Flags {
  CARRY_FLAG = 1 << 0,
  RESERVED1 = 1 << 1,
  PARITY_FLAG = 1 << 2,
  RESERVED2 = 1 << 3,
  AUXILIARY_CARRY_FLAG = 1 << 4,
  RESERVED3 = 1 << 5,
  ZERO_FLAG = 1 << 6,
  SIGN_FLAG = 1 << 7,
  TRAP_FLAG = 1 << 8,
  INTERRUPT_ENABLE_FLAG = 1 << 9,
  DIRECTION_FLAG = 1 << 10,
  OVERFLOW_FLAG = 1 << 11,
  IOPL1 = 1 << 12,
  IOPL2 = 1 << 13,
  NESTED_TASK_FLAG = 1 << 14,
  RESERVED4 = 1 << 15,

  RESUME_FLAG = 1 << 16
};
// clang-format on
