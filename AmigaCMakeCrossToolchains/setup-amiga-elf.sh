#!/bin/bash
# Make the script executable with: chmod +x setup-amiga-elf.sh

# Script to set up amiga-elf.cmake platform file for CMake
# This script creates the necessary platform file for using amiga-elf with CMake

set -e  # Exit on any error

echo "Setting up amiga-elf.cmake for CMake..."

# Find CMake version and extract only major.minor (e.g., 3.28 from 3.28.4)
CMAKE_FULL_VERSION=$(cmake --version | head -n1 | awk '{print $3}')
if [ -z "$CMAKE_FULL_VERSION" ]; then
    echo "Error: CMake not found or version could not be determined."
    exit 1
fi

# Extract only major.minor version (e.g., 3.28 from 3.28.4)
CMAKE_VERSION=$(echo "$CMAKE_FULL_VERSION" | cut -d. -f1,2)
if [ -z "$CMAKE_VERSION" ]; then
    echo "Error: Could not extract major.minor version from $CMAKE_FULL_VERSION"
    exit 1
fi

echo "Detected CMake version: $CMAKE_VERSION"

# Determine the CMake modules platform directory
CMAKE_PLATFORM_DIR="/usr/share/cmake-${CMAKE_VERSION}/Modules/Platform"

# Check if the directory exists
if [ ! -d "$CMAKE_PLATFORM_DIR" ]; then
    echo "CMake platform directory not found at: $CMAKE_PLATFORM_DIR"
    
    # Try alternate location
    CMAKE_PLATFORM_DIR="/usr/local/share/cmake-${CMAKE_VERSION}/Modules/Platform"
    
    if [ ! -d "$CMAKE_PLATFORM_DIR" ]; then
        echo "Could not find CMake platform directory."
        echo "Please manually copy Generic.cmake to amiga-elf.cmake in your CMake platform directory."
        exit 1
    fi
fi

echo "Found CMake platform directory: $CMAKE_PLATFORM_DIR"

# Check if Generic.cmake exists
GENERIC_CMAKE_FILE="${CMAKE_PLATFORM_DIR}/Generic.cmake"

if [ ! -f "$GENERIC_CMAKE_FILE" ]; then
    echo "Error: Generic.cmake not found at: $GENERIC_CMAKE_FILE"
    exit 1
fi

# Create amiga-elf.cmake
AMIGA_ELF_CMAKE_FILE="${CMAKE_PLATFORM_DIR}/amiga-elf.cmake"

# Check if we need sudo
if [ -w "$CMAKE_PLATFORM_DIR" ]; then
    # We have write permissions
    cp "$GENERIC_CMAKE_FILE" "$AMIGA_ELF_CMAKE_FILE"
    echo "Created: $AMIGA_ELF_CMAKE_FILE"
else
    # We need sudo
    echo "Need elevated permissions to create $AMIGA_ELF_CMAKE_FILE"
    sudo cp "$GENERIC_CMAKE_FILE" "$AMIGA_ELF_CMAKE_FILE"
    echo "Created: $AMIGA_ELF_CMAKE_FILE (with sudo)"
fi

echo "Setup complete! You can now use the m68k-bartman.cmake toolchain with amiga-elf."