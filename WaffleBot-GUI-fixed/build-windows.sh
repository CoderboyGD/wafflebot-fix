#!/bin/bash
# Run this inside the "MSYS2 UCRT64" shell (see README.md). Produces dist/WaffleBot-GUI.exe
set -e
pacman -S --needed --noconfirm \
  mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja \
  mingw-w64-ucrt-x86_64-pkgconf mingw-w64-ucrt-x86_64-qt6-base mingw-w64-ucrt-x86_64-libsndfile
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
mkdir -p dist
cp build/WaffleBot-GUI.exe dist/
windeployqt6 --no-translations dist/WaffleBot-GUI.exe || true
ldd dist/WaffleBot-GUI.exe | grep -i ucrt64 | awk '{print $3}' | xargs -I{} cp -n {} dist/
echo
echo "Done. Your program is in: $(pwd)/dist/WaffleBot-GUI.exe"
