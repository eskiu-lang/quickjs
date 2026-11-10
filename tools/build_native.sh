#!/bin/sh
# Builds libeskqjs.a (QuickJS plus native/eskqjs.c) for each platform of eskiu.toml's
# [native.eskqjs] table and packs one archive per platform into build/native/.
#
#     tools/build_native.sh                     # every platform this machine can build
#     tools/build_native.sh macos-arm64 ...     # only these
#     tools/build_native.sh --install macos-arm64
#     tools/build_native.sh --seed linux-x86_64
#
# --install also unpacks each archive into $ESKIU_HOME/native/SHA256 (default
# ~/.eskiu/native), where eskiuc looks before downloading, so a build links the library
# just made. build/native/SHA256SUMS lists the sums; tools/update_manifest.py copies them
# into eskiu.toml.
# --seed unpacks each archive under the SHA-256 that eskiu.toml already names for its
# platform instead, so eskiuc links the library built from this checkout in place of the
# published one (CI, and work before a release; another compiler gives other bytes).
#
# Toolchains, each overridable by its variable:
#   macOS, iOS     xcrun clang (Xcode)
#   Android        NDK clang from $ANDROID_NDK_HOME, else the newest under the SDK
#   Windows        llvm-mingw ($MINGW_ROOT, default: the one eskiuc installs), or
#                  x86_64-w64-mingw32-gcc on PATH
#   Linux          $LINUX_CC, else `zig cc` ($ZIG), else cc on a Linux host
set -eu

cd "$(dirname "$0")/.."
ROOT=$(pwd)
VERSION=$(sed -n 's/^version = "\(.*\)"/\1/p' eskiu.toml | head -1)
OUT="$ROOT/build/native"
INSTALL=0
SEED=0
ALL="macos-arm64 macos-x86_64 linux-x86_64 linux-arm64 windows-x86_64 windows-arm64 \
ios-arm64 ios-arm64-simulator ios-x86_64-simulator android-arm64 android-x86_64"

if [ "${1:-}" = "--install" ]; then INSTALL=1; shift; fi
if [ "${1:-}" = "--seed" ]; then SEED=1; shift; fi
PLATFORMS=${*:-$ALL}

QJS_VERSION=$(cat native/quickjs/VERSION)
SRCS="native/quickjs/quickjs.c native/quickjs/libregexp.c native/quickjs/libunicode.c \
native/quickjs/cutils.c native/quickjs/libbf.c native/eskqjs.c"
# The defines rquickjs-sys 0.6.2 builds QuickJS with. QuickJS's own assertions stay on, as
# there (no NDEBUG).
CFLAGS_COMMON="-O2 -D_GNU_SOURCE -DCONFIG_BIGNUM -DCONFIG_VERSION=\"$QJS_VERSION\" \
-ffile-prefix-map=$ROOT/= -fno-ident -w"

LLVM_AR=${LLVM_AR:-}
if [ -z "$LLVM_AR" ]; then
    for c in llvm-ar /opt/homebrew/opt/llvm/bin/llvm-ar /usr/local/opt/llvm/bin/llvm-ar \
             "$(xcrun -f llvm-ar 2>/dev/null || true)"; do
        if [ -n "$c" ] && command -v "$c" >/dev/null 2>&1; then LLVM_AR=$c; break; fi
    done
fi

ndk_root() {
    if [ -n "${ANDROID_NDK_HOME:-}" ]; then echo "$ANDROID_NDK_HOME"; return; fi
    sdk=${ANDROID_HOME:-$HOME/Library/Android/sdk}
    [ -d "$sdk/ndk" ] || sdk=$HOME/Android/Sdk
    ls -d "$sdk"/ndk/* 2>/dev/null | sort -V | tail -1
}

mingw_root() {
    if [ -n "${MINGW_ROOT:-}" ]; then echo "$MINGW_ROOT"; return; fi
    ls -d "${ESKIU_HOME:-$HOME/.eskiu}"/toolchains/llvm-mingw-* 2>/dev/null | tail -1
}

# Prints "CC ARFORMAT EXTRA_FLAGS" for a platform, or nothing when no toolchain is found.
toolchain() {
    case $1 in
    macos-arm64) echo "xcrun:clang darwin -arch arm64 -mmacosx-version-min=11.0" ;;
    macos-x86_64) echo "xcrun:clang darwin -arch x86_64 -mmacosx-version-min=10.15" ;;
    ios-arm64) echo "xcrun-iphoneos:clang darwin -arch arm64 -miphoneos-version-min=15.0" ;;
    ios-arm64-simulator) echo "xcrun-iphonesimulator:clang darwin -arch arm64 -mios-simulator-version-min=15.0" ;;
    ios-x86_64-simulator) echo "xcrun-iphonesimulator:clang darwin -arch x86_64 -mios-simulator-version-min=15.0" ;;
    android-arm64|android-x86_64)
        ndk=$(ndk_root)
        [ -n "$ndk" ] || return 0
        bin=$(ls -d "$ndk"/toolchains/llvm/prebuilt/*/bin | head -1)
        arch=aarch64; [ "$1" = android-x86_64 ] && arch=x86_64
        echo "$bin/$arch-linux-android24-clang gnu -fPIC" ;;
    windows-x86_64|windows-arm64)
        arch=x86_64; [ "$1" = windows-arm64 ] && arch=aarch64
        m=$(mingw_root)
        if [ -n "$m" ] && [ -x "$m/bin/$arch-w64-mingw32-clang" ]; then
            # no COFF time stamp, so the archive is the same on every build
            echo "$m/bin/$arch-w64-mingw32-clang gnu -mno-incremental-linker-compatible"
        elif command -v $arch-w64-mingw32-gcc >/dev/null 2>&1; then
            echo "$arch-w64-mingw32-gcc gnu -Wa,--no-insert-timestamp"
        fi ;;
    linux-x86_64|linux-arm64)
        arch=x86_64; [ "$1" = linux-arm64 ] && arch=aarch64
        if [ -n "${LINUX_CC:-}" ]; then echo "$LINUX_CC gnu -fPIC"
        elif command -v "${ZIG:-zig}" >/dev/null 2>&1; then
            echo "zig:${ZIG:-zig} gnu -fPIC -target $arch-linux-gnu.2.17"
        elif [ "$(uname -s)" = Linux ] && [ "$(uname -m)" = "$arch" ]; then
            echo "cc gnu -fPIC"
        fi ;;
    *) echo "unknown platform $1" >&2; exit 2 ;;
    esac
}

compile() {   # compile CC SRC OBJ FLAGS...
    cc=$1; src=$2; obj=$3; shift 3
    case $cc in
    xcrun:*) xcrun clang $CFLAGS_COMMON "$@" -c "$src" -o "$obj" ;;
    xcrun-*) sdk=${cc#xcrun-}; sdk=${sdk%%:*}; xcrun -sdk "$sdk" clang $CFLAGS_COMMON "$@" -c "$src" -o "$obj" ;;
    zig:*) "${cc#zig:}" cc $CFLAGS_COMMON "$@" -c "$src" -o "$obj" ;;
    *) "$cc" $CFLAGS_COMMON "$@" -c "$src" -o "$obj" ;;
    esac
}

sha256() { if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | cut -d' ' -f1; else shasum -a 256 "$1" | cut -d' ' -f1; fi; }

mkdir -p "$OUT"
: > "$OUT/SHA256SUMS.new"
for p in $PLATFORMS; do
    tc=$(toolchain "$p")
    if [ -z "$tc" ]; then echo "skip $p: no toolchain"; continue; fi
    set -- $tc
    cc=$1; fmt=$2; shift 2
    work="$OUT/$p"
    rm -rf "$work"; mkdir -p "$work/obj" "$work/pkg/lib" "$work/pkg/include/quickjs"
    objs=""
    for s in $SRCS; do
        o="$work/obj/$(basename "$s" .c).o"
        compile "$cc" "$s" "$o" "$@"
        objs="$objs $o"
    done
    if [ -n "$LLVM_AR" ]; then
        "$LLVM_AR" rcsD --format="$fmt" "$work/pkg/lib/libeskqjs.a" $objs
    else
        ar rcs "$work/pkg/lib/libeskqjs.a" $objs
    fi
    cp native/eskqjs.h "$work/pkg/include/"
    cp native/quickjs/quickjs.h "$work/pkg/include/quickjs/"
    cp native/quickjs/LICENSE "$work/pkg/LICENSE.quickjs"
    cp native/patches/LICENSE "$work/pkg/LICENSE.rquickjs"
    cp LICENSE "$work/pkg/LICENSE"
    # A stable archive: fixed times, owners and order, gzip without a timestamp.
    find "$work/pkg" -exec touch -h -t 202601010000 {} +
    name="eskqjs-$VERSION-$p.tar.gz"
    (cd "$work/pkg" && find . -type f | LC_ALL=C sort > ../files.txt &&
        tar --uid 0 --gid 0 --uname root --gname root -cf - -T ../files.txt 2>/dev/null ||
        tar --owner 0 --group 0 --numeric-owner -cf - -T ../files.txt) | gzip -n -9 > "$OUT/$name"
    sum=$(sha256 "$OUT/$name")
    echo "$sum  $name" >> "$OUT/SHA256SUMS.new"
    echo "built $p: $name $sum"
    if [ $SEED = 1 ]; then
        named=$(sed -n "s/^$p = .*sha256 = \"\([0-9a-f]*\)\".*/\1/p" eskiu.toml)
        if [ -z "$named" ]; then echo "eskiu.toml has no $p entry" >&2; exit 1; fi
        dest="${ESKIU_HOME:-$HOME/.eskiu}/native/$named"
        rm -rf "$dest"; mkdir -p "$dest"
        tar -xzf "$OUT/$name" -C "$dest"
        echo "seeded $p into $dest"
    fi
    if [ $INSTALL = 1 ]; then
        dest="${ESKIU_HOME:-$HOME/.eskiu}/native/$sum"
        rm -rf "$dest"; mkdir -p "$dest"
        tar -xzf "$OUT/$name" -C "$dest"
        echo "installed $p into $dest"
    fi
done
if [ -f "$OUT/SHA256SUMS" ]; then
    # keep the sums of platforms not rebuilt this time
    cut -d' ' -f3 "$OUT/SHA256SUMS.new" > "$OUT/.names"
    grep -v -F -f "$OUT/.names" "$OUT/SHA256SUMS" >> "$OUT/SHA256SUMS.new" || true
    rm -f "$OUT/.names"
fi
LC_ALL=C sort -k2 "$OUT/SHA256SUMS.new" > "$OUT/SHA256SUMS"
rm -f "$OUT/SHA256SUMS.new"
