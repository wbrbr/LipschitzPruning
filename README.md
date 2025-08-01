# Lipschitz Pruning

## Dependencies

- Vulkan SDK

## Build

* Install the Vulkan SDK. (Ubuntu instructions: `apt install libvulkan-dev libvulkan-memory-allocator-dev glslc libglfw3-dev`)
* Fetch the submodules: `git submodule update --init --recursive`
* Build:
```
mkdir build
cd build
cmake ..
cmake --build .
```

## Run

The executable should be run from the `build` folder. Examples:
* Linux build: `./LipschitzPruning`
* Windows build (Debug): `Debug\LipschitzPruning.exe`