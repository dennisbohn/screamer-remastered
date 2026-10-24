#!/bin/sh
# The Windows x86-64 release: screamer-remastered.exe built with MinGW-w64 on Linux (SDL2
# built into it, port/sdl_lean.cmake), a README and the licences, as
#   dist/screamer-remastered-<version>-windows-x86_64.zip
# usage: tools/release_windows.sh      (needs x86_64-w64-mingw32-gcc, cmake, the network once)
set -e
cd "$(dirname "$0")/.."
ROOT=$PWD
V=$(sh tools/version.sh)
NAME=screamer-remastered-$V-windows-x86_64
W=build/release-windows

rm -rf $W/game "dist/$NAME"
mkdir -p $W/game "dist/$NAME"
cat > $W/toolchain.cmake <<EOT
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)
set(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
EOT
cmake -S port -B $W/game -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE=$ROOT/$W/toolchain.cmake > $W/cmake.log
cmake --build $W/game -j"$(nproc)" 2>&1 | grep -E ' error|warning: implicit|Built target screamer-remastered'

cp $W/game/screamer-remastered.exe "dist/$NAME/"
cp LICENSE "dist/$NAME/LICENSE.txt"
cp port/src/nuked/opl3.h "dist/$NAME/LICENSE-nuked-opl3.txt"
cp $W/game/_deps/sdl2_lean-src/LICENSE.txt "dist/$NAME/LICENSE-SDL2.txt"
sed "s/@VERSION@/$V/" tools/release_windows_readme.txt | sed 's/$/\r/' > "dist/$NAME/README.txt"
rm -f "dist/$NAME.zip"
(cd dist && python3 -m zipfile -c "$NAME.zip" "$NAME")
echo "what it needs:"
x86_64-w64-mingw32-objdump -p "dist/$NAME/screamer-remastered.exe" | grep "DLL Name"
ls -la "dist/$NAME.zip"
