#!/bin/bash

cflags="-Wall -O3 -g -std=gnu11 -fno-strict-aliasing -Isrc"
lflags="-lSDL2 -lm"

if [[ $* == *windows* ]]; then
  platform="windows"
  outfile="lite.exe"
  compiler="x86_64-w64-mingw32-gcc"
  cflags="$cflags -DLUA_USE_POPEN -Iwinlib/SDL2-2.0.10/x86_64-w64-mingw32/include"
  lflags="$lflags -Lwinlib/SDL2-2.0.10/x86_64-w64-mingw32/lib"
  lflags="$lflags -lole32"

if [[ $* == *linux* ]]; then
  helper_src="src/api/folder_chooser_linux.c"
  if [ -f "$helper_src" ] && pkg-config --exists gtk4 2>/dev/null; then
    helper_bin="lite_folder_chooser_helper"
    gtk_cflags=$(pkg-config --cflags gtk4)
    gtk_libs=$(pkg-config --libs gtk4)
    echo "compiling folder chooser helper..."
    if ccache gcc $gtk_cflags -O2 $helper_src -o "$helper_bin" $gtk_libs 2>/dev/null; then
      echo "helper built"
    else
      echo "helper build failed, native folder dialog will be unavailable on Linux"
      rm -f "$helper_bin"
    fi
  fi
fi
  lflags="-lmingw32 -lSDL2main $lflags -mwindows -o $outfile res.res"
  x86_64-w64-mingw32-windres res.rc -O coff -o res.res
else
  platform="unix"
  outfile="lite"
  compiler="gcc"
  cflags="$cflags -DLUA_USE_POSIX"
  lflags="$lflags -o $outfile"
fi

if command -v ccache >/dev/null; then
  compiler="ccache $compiler"
fi


echo "compiling ($platform)..."
for f in `find src -name "*.c"`; do
  $compiler -c $cflags $f -o "${f//\//_}.o"
  if [[ $? -ne 0 ]]; then
    got_error=true
  fi
done

if [[ ! $got_error ]]; then
  echo "linking..."
  $compiler *.o $lflags
fi

echo "cleaning up..."
rm *.o
rm res.res 2>/dev/null
echo "done"
