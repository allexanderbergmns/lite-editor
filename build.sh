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

# Optional GTK4 folder chooser helper, used by system.show_open_folder_dialog_native.
# It is a separate process so the main binary never links against GTK. Skipped
# silently when GTK4 development files are not installed.
if [[ $platform == "unix" ]]; then
  helper_src="folder_chooser_linux_helper.c"
  if [ -f "$helper_src" ] && pkg-config --atleast-version=4.10 gtk4 2>/dev/null; then
    echo "compiling folder chooser helper..."
    if $compiler $(pkg-config --cflags gtk4) -O2 "$helper_src" \
        -o lite_folder_chooser_helper $(pkg-config --libs gtk4); then
      echo "helper built"
    else
      rm -f lite_folder_chooser_helper
      echo "helper build failed, native folder dialog unavailable on Linux"
    fi
  else
    echo "gtk4 >= 4.10 not found, native folder dialog unavailable on Linux"
  fi
fi

echo "cleaning up..."
rm *.o
rm res.res 2>/dev/null
echo "done"
