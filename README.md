> [!IMPORTANT]
> **License Notice:** RiftCrack is licensed under the [Anyone But the RiftBot Staff (ABRS) License](/COPYING.md). If you believe you may fall within the license's restricted class, please [review the full license](/COPYING.md) to understand the rights afforded to you.
>
> **License Transition::** Under [Section 8 of the ABRS License](/COPYING.md#8-transition-to-the-mit-license), the Software will transition to the standard [MIT License](https://opensource.org/license/mit) once RiftBot's changes to `hyper-reV` are properly licensed and published in accordance with the `hyper-reV` licensing requirements.

# RiftCrack

[Join the Telegram group chat](https://t.me/riftcrack) for help/support.

[Download the latest version](https://github.com/riftcr/RiftBot-Crack/releases/latest)

## Build instructions

### Prerequisites
* [Git for Windows](https://github.com/git-for-windows/git/releases/latest) installed to your PATH
* [CMake](https://cmake.org/) installed to your PATH
* [Ninja](https://ninja-build.org/) installed to your PATH
* [LLVM/Clang 22+](https://github.com/llvm/llvm-project/releases/download/llvmorg-22.1.8/LLVM-22.1.8-win64.exe) installed to your PATH
* Visual Studio/Windows SDK

### Compiling

Ensure you have installed the build prerequisites and that you cloned the repository correctly.

```pwsh
# Clone repository
git clone --recursive https://github.com/riftcr/RiftBot-Crack.git

# Change directory to newly cloned repo
cd RiftBot-Crack

# Configure CMake project
cmake --preset release

# Compile
cmake --build --preset release

# Copy built binaries to current directory
copy build/release/src/rc.dll .
copy build/release/launcher/riftcrack-launcher.exe .
```
