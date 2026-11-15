#!/bin/bash
# Build Mesa's OSMesa (OpenGL ES 2 off-screen) for iOS arm64 with the panfrost driver (Midgard v5,
# the J7 Prime's Mali-T830) and softpipe, on macOS with Xcode's iphoneos SDK (.github/workflows/mesa.yml).
# panfrost talks to the kernel's HakiosMali (xnu patch 0193) through hakios-drm, which stands in for
# libdrm. Patches and hakios-drm come from the Galaxy A50 port (see README.md here).
#
#   tools/mesa/mk-mesa-ios.sh OUT       OUT gets libOSMesa.8.dylib and libglapi.0.dylib, install names
#                                       @rpath/... (the phone has them beside gl_test in /usr/local/bin: the
#                                       kernel's file installer makes no directories), stripped (-x), ad-hoc signed
# Size matters: the files ride inside the kernel image (32 MiB BOOT partition), so -Os and -dead_strip.
set -euo pipefail
VERSION=25.0.7
SHA256=592272df3cf01e85e7db300c449df5061092574d099da275d19e97ef0510f8a6
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=$(mkdir -p "$1" && cd "$1" && pwd)
W=${WORK:-$PWD/mesa-work}
DRIVERS=${DRIVERS:-softpipe,panfrost}
mkdir -p "$W"
cd "$W"

[ -f mesa-$VERSION.tar.xz ] || curl -sL --fail -o mesa-$VERSION.tar.xz https://archive.mesa3d.org/mesa-$VERSION.tar.xz
echo "$SHA256  mesa-$VERSION.tar.xz" | shasum -a 256 -c
rm -rf mesa-$VERSION build
tar xJf mesa-$VERSION.tar.xz
# order matters: panfrost-darwin.patch goes on top of freedreno-darwin.patch (the libdrm stand-in, osmesa_target.c)
for p in freedreno-darwin osmesa-es-profile panfrost-darwin panfrost-pack-pointer panfrost-tiler-heap; do
	patch -s --no-backup-if-mismatch -d mesa-$VERSION -p1 < "$HERE/$p.patch"
done
cp -r "$HERE/hakios-drm" mesa-$VERSION/src/hakios-drm

[ -x venv/bin/python3 ] || { python3 -m venv venv && venv/bin/pip -q install mako pyyaml packaging meson; }
export PATH=$W/venv/bin:$PATH

SDK=$(xcrun --sdk iphoneos --show-sdk-path)
CC=$(xcrun --sdk iphoneos --find clang)
CXX=$(xcrun --sdk iphoneos --find clang++)
cat > cross.ini <<INI
[binaries]
c = ['$CC', '-arch', 'arm64', '-isysroot', '$SDK', '-miphoneos-version-min=16.0']
cpp = ['$CXX', '-arch', 'arm64', '-isysroot', '$SDK', '-miphoneos-version-min=16.0']
objc = ['$CC', '-arch', 'arm64', '-isysroot', '$SDK', '-miphoneos-version-min=16.0']
ar = '$(xcrun --sdk iphoneos --find ar)'
strip = '$(xcrun --sdk iphoneos --find strip)'
pkg-config = 'false'

[host_machine]
system = 'darwin'
subsystem = 'ios'
cpu_family = 'aarch64'
cpu = 'arm64'
endian = 'little'

[properties]
needs_exe_wrapper = true
INI

meson setup build mesa-$VERSION --cross-file cross.ini --wrap-mode=nofallback \
	--prefix=/usr/local --libdir=lib --buildtype=release -Doptimization=s -Db_ndebug=true \
	-Dc_link_args=-Wl,-dead_strip -Dcpp_link_args=-Wl,-dead_strip \
	-Dplatforms= -Dgallium-drivers="$DRIVERS" -Dvulkan-drivers= -Dglx=disabled -Degl=disabled -Dgbm=disabled \
	-Dosmesa=true -Dllvm=disabled -Dshared-glapi=enabled -Dgles1=disabled -Dgles2=enabled -Dopengl=true \
	-Dzstd=disabled -Dxmlconfig=disabled -Dexpat=disabled -Dshader-cache=disabled -Dvalgrind=disabled \
	-Dlibunwind=disabled -Dglvnd=disabled -Dvideo-codecs= -Dtools= -Dgallium-vdpau=disabled -Dgallium-va=disabled \
	-Dgallium-xa=disabled -Dlmsensors=disabled -Dbuild-tests=false
ninja -C build src/gallium/targets/osmesa/libOSMesa.8.dylib

# the library and what it needs that is not the system's (@rpath/..., found by the program's rpath)
osm=build/src/gallium/targets/osmesa/libOSMesa.8.dylib
otool -L $osm
cp $osm "$OUT/libOSMesa.8.dylib"
for dep in $(otool -L $osm | awk 'NR > 1 { print $1 }' | grep -v '^/usr/lib/\|^/System/'); do
	name=$(basename "$dep")
	[ "$name" != libOSMesa.8.dylib ] || continue    # (its own install name, the first line)
	src=$(find build -name "$name" -type f | head -1)
	[ -n "$src" ] || { echo "missing $dep"; exit 1; }
	cp "$src" "$OUT/$name"
	install_name_tool -change "$dep" @rpath/$name "$OUT/libOSMesa.8.dylib"
done
# SYMS=DIR: the libraries as built, before strip -x (symbols of the static functions, for eagl's
# glprof=US profile lines "hakios-prof: libOSMesa.8.dylib+0xOFFSET": the same offsets, strip changes no code)
if [ -n "${SYMS:-}" ]; then
	mkdir -p "$SYMS"
	cp "$OUT"/*.dylib "$SYMS"/
fi
for f in "$OUT"/*.dylib; do
	strip -x "$f"
	install_name_tool -id @rpath/$(basename "$f") "$f"
	codesign -f -s - "$f"
done
otool -L "$OUT"/*.dylib
ls -l "$OUT"
