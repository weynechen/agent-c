This is an agent library written in C

## Preqruiment
`cmake`
`curl` : vcpkg install 

## Build

### Windows (Visual Studio)
```bash
mkdir build && cd build
cmake ..
cmake --build . --config Release
```

### Linux/macOS/Windows MinGW
```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
```

