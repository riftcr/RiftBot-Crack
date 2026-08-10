#pragma once
#include <wil/win32_helpers.h>

inline void EnumerateThreads(
    std::move_only_function<void(_SYSTEM_THREAD_INFORMATION& info)> callback) {
  auto buf_len = 0x1000;
  auto buf = std::make_unique_for_overwrite<char[]>(buf_len);

  do {
    ULONG retn_len;

    if (auto status = NtQuerySystemInformation(SystemProcessInformation, (void*)buf.get(), buf_len,
                                               &retn_len);
        !NT_SUCCESS(status)) {
      if (status == STATUS_INFO_LENGTH_MISMATCH) {
        // DLOGF(INFO, "STATUS_INFO_LENGTH_MISMATCH: {} vs wrote {}", buf_len, retn_len);
        buf_len = retn_len ? retn_len : buf_len + 0x1000;
        buf = std::make_unique_for_overwrite<char[]>(buf_len);
      } else {
        THROW_NTSTATUS(status);
      }
    } else {
      // DLOGF(INFO, "OK: {} vs wrote {}", buf_len, retn_len);
      buf_len = retn_len;
      break;
    }
  } while (true);

  auto info = reinterpret_cast<SYSTEM_PROCESS_INFORMATION*>(buf.get());
  do {
    if (!info->ImageName.Length) continue;

    if (info->UniqueProcessId == NtCurrentProcessId()) {
      VLOGF(1, "found self: {}", (uintp)info->UniqueProcessId);
      for (auto& thread : std::span{info->Threads, info->NumberOfThreads}) {
        VLOGF(1, "thread id={}  state={}  wait_reason={}", (uintp)thread.ClientId.UniqueThread,
              std::to_underlying(thread.ThreadState), std::to_underlying(thread.WaitReason));
        callback(thread);
      }
    }
    if (!info->NextEntryOffset) break;
  } while ((info = (SYSTEM_PROCESS_INFORMATION*)((uintp)info + info->NextEntryOffset)));
}
