#pragma once
#include <array>
#include <io.h>
#include <fcntl.h>
#include <iostream>

class Console2 {
 public:
  inline Console2() {
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) {
      AllocConsole();
    }
    // Open fresh Win32 handles each time
    HANDLE hIn = CreateFileA("CONIN$", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                             OPEN_EXISTING, 0, nullptr);
    HANDLE hOut = CreateFileA("CONOUT$", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_EXISTING, 0, nullptr);
    HANDLE hErr = CreateFileA("CONOUT$", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_EXISTING, 0, nullptr);

    SetStdHandle(STD_INPUT_HANDLE, hIn);
    SetStdHandle(STD_OUTPUT_HANDLE, hOut);
    SetStdHandle(STD_ERROR_HANDLE, hErr);

    // Re-sync CRT stdio to the new Win32 handles
    arr_[0] = _fdopen(_open_osfhandle(reinterpret_cast<intptr_t>(hIn), _O_RDONLY), "rb");
    arr_[1] = _fdopen(_open_osfhandle(reinterpret_cast<intptr_t>(hOut), _O_WRONLY), "wb");
    arr_[2] = _fdopen(_open_osfhandle(reinterpret_cast<intptr_t>(hErr), _O_WRONLY), "wb");

    *stdin = *arr_[0];
    *stdout = *arr_[1];
    *stderr = *arr_[2];
  }
  inline ~Console2() {
    for (auto& f : arr_) {
      if (f) {
        fclose(f);
        f = nullptr;
      }
    }
    // Do NOT call FreeConsole() — leave the console alive
  }

 private:
  std::array<FILE*, 3> arr_;
};

class Console {
 public:
  Console() {
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) {
      AllocConsole();
    }

    FILE* fDummy;
    freopen_s(&fDummy, "CONOUT$", "w", stdout);
    freopen_s(&fDummy, "CONOUT$", "w", stderr);
    freopen_s(&fDummy, "CONIN$", "r", stdin);
    std::cout.clear();
    std::clog.clear();
    std::cerr.clear();
    std::cin.clear();

    // std::wcout, std::wclog, std::wcerr, std::wcin
    HANDLE hConOut =
        CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    HANDLE hConIn =
        CreateFileW(L"CONIN$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    SetStdHandle(STD_OUTPUT_HANDLE, hConOut);
    SetStdHandle(STD_ERROR_HANDLE, hConOut);
    SetStdHandle(STD_INPUT_HANDLE, hConIn);
    std::wcout.clear();
    std::wclog.clear();
    std::wcerr.clear();
    std::wcin.clear();
  }
  ~Console() {}
};
