# ArC

一个用 C 语言编写的 Agent 库。支持hosted(Windows/Linux/MacOS)环境和RTOS环境，仅需依赖POSIX Thread，以及http连接。[[README_CN.md]]

## 依赖

- `cmake` (>= 3.14)
- `libcurl`

## 构建

### Windows

#### 安装依赖

使用 vcpkg 安装 curl：

```powershell
vcpkg install curl:x64-windows
```

#### 方式1：使用 Ninja 生成器（推荐，支持 clangd）

```powershell
mkdir build && cd build
cmake -G Ninja -DCMAKE_TOOLCHAIN_FILE=<vcpkg路径>/scripts/buildsystems/vcpkg.cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=ON ..
cmake --build . --config Release
```

> 如果没有 Ninja，可以通过 `winget install Ninja-build.Ninja` 安装

#### 方式2：使用 Visual Studio 生成器

```powershell
mkdir build && cd build
cmake -DCMAKE_TOOLCHAIN_FILE=<vcpkg路径>/scripts/buildsystems/vcpkg.cmake ..
cmake --build . --config Release
```

### Linux/macOS

```bash
# 安装依赖 (Ubuntu/Debian)
sudo apt install libcurl4-openssl-dev

# 构建
mkdir build && cd build
cmake ..
make -j$(nproc)
```

## 运行示例

```bash
cd build/Release  # Windows
# 或
cd build          # Linux/macOS

# 设置环境变量（或创建 .env 文件）
# OPENAI_API_KEY=your-api-key
# OPENAI_BASE_URL=https://api.openai.com/v1  # 可选
# OPENAI_MODEL=gpt-4o-mini                   # 可选

# 运行 demo
./chat_demo
./chat_markdown
./chat_tools "计算 199*89"
```
