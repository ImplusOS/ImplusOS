# Vendor Library Management

This directory contains external libraries ported to ImplusOS.
All libraries should be managed dynamically by the `Makefile` in this directory.

## How to add a new library
1. Place the library source in a subdirectory.
2. Update the `Makefile` to include the library in the build process.
3. The built library (e.g., `libz.a`) will be placed in `Build/Vendor/Library/`.

## Usage
The top-level `Makefile` triggers the build of this directory.
Libraries should be accessed by Userland applications via the provided APIs in `Userland/API/`.
