# AmigaCMakeCrossToolchains

This repo contains CMake toolchain files for configuring CMake-based projects to build binaries for Amiga-like systems.

Typical usage is as follows:

```shell
cd /project/to/build
mkdir build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=/path/to/toolchainFile.cmake -D<TOOLCHAIN_FILE_SPECIFIC_DEFINES>
```

## Setting up the CMake for m68k-amigaos and m68k-bartman targets

In order to use the m68k-amigaos.cmake or m68k-bartman.cmake toolchain files, you need to do a tiny change to your CMake install:

- enter the directory where the CMake platform configuration files are stored - specifically the Generic.cmake file.
  - On GNU/Linux systems, this is usually stored in `/usr/share/cmake-<VERSION>/Modules/Platforms/Generic.cmake`
  - On Windows systems, this is usually stored in  `C:\Program Files\CMake\share\cmake-<VERSION>\Modules\Platform\Generic.cmake`
- copy `Generic.cmake` to `amiga-elf.cmake`

this is due to toolchain file changing `CMAKE_SYSTEM_NAME` to `amiga-elf`. On unchanged installations, this could throw an error because there's no platform file.

## Using the setup scripts

To simplify the setup process, you can use the provided scripts:

### On Linux:
```bash
# Make the script executable
chmod +x setup-amiga-elf.sh
# Run the script (may require sudo)
./setup-amiga-elf.sh
```

### On Windows:
```
# Right-click on setup-amiga-elf.bat and select "Run as administrator"
```

These scripts will automatically copy the Generic.cmake file to amiga-elf.cmake in your CMake installation, making it ready to use with the m68k-bartman.cmake toolchain.
