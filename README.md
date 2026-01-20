# AgentC

An Agent library written in C. Supports both hosted(windows/Linux/macos) and RTOS environments, requiring only POSIX Thread and HTTP connection dependencies.

## Dependencies

- `cmake` (>= 3.14)

hosted:
- `libcurl`

## Build

### Windows

#### Install Dependencies

Install curl using vcpkg:

```powershell
vcpkg install curl:x64-windows
```

#### Option 1: Using Ninja Generator (Recommended, supports clangd)

```powershell
mkdir build && cd build
cmake -G Ninja -DCMAKE_TOOLCHAIN_FILE=<vcpkg-path>/scripts/buildsystems/vcpkg.cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=ON ..
cmake --build . --config Release
```

> If you don't have Ninja, install it via `winget install Ninja-build.Ninja`

#### Option 2: Using Visual Studio Generator

```powershell
mkdir build && cd build
cmake -DCMAKE_TOOLCHAIN_FILE=<vcpkg-path>/scripts/buildsystems/vcpkg.cmake ..
cmake --build . --config Release
```

### Linux/macOS

```bash
# Install dependencies (Ubuntu/Debian)
sudo apt install libcurl4-openssl-dev

# Build
mkdir build && cd build
cmake ..
make -j$(nproc)
```

## Run Examples

```bash
cd build/Release  # Windows
# or
cd build          # Linux/macOS

# Set environment variables (or create a .env file)
# OPENAI_API_KEY=your-api-key
# OPENAI_BASE_URL=https://api.openai.com/v1  # Optional
# OPENAI_MODEL=gpt-4o-mini                   # Optional

# Run demos
./chat_demo
./chat_markdown
./chat_tools "compute 199*89"
```
