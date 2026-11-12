#!/usr/bin/env bash
# Builds dist/tetris-macos.dmg: a disk image with Tetris.app and a link to
# /Applications to drag it to. The app is one universal executable (Apple
# silicon and Intel) with SDL3 linked in statically and the shaders built in,
# for macOS 11 or later.
#
# Runs on a Mac with the Xcode command line tools, CMake and Git. SDL3 is built
# from source into build/macos-release the first time and reused after that.
#
# The app is signed ad hoc, which Apple silicon requires of every executable,
# but not with a Developer ID or notarized, so Gatekeeper asks once before the
# first launch.
set -euo pipefail

# Keep in step with packaging/linux/Dockerfile and packaging/windows/Dockerfile.
sdl_version=3.4.20
deployment_target=11.0
architectures="arm64;x86_64"
bundle_id=io.github.ibogdanchikov.tetris

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
work="$repo_root/build/macos-release"
sdl_prefix="$work/sdl3-$sdl_version"
app="$work/Tetris.app"
output="$repo_root/dist/tetris-macos.dmg"
jobs="$(sysctl -n hw.ncpu)"
version="$(sed -n 's/^project(tetris VERSION \([0-9.]*\).*/\1/p' "$repo_root/CMakeLists.txt")"

# On GitHub Actions, results also go out as annotations, which show on the
# run's page without opening the log.
notice() {
    if [[ -n "${GITHUB_ACTIONS:-}" ]]; then
        echo "::notice::$*"
    fi
    echo "$*"
}
fail() {
    if [[ -n "${GITHUB_ACTIONS:-}" ]]; then
        echo "::error::$*"
    fi
    echo "error: $*" >&2
    exit 1
}

if [[ ! -d "$sdl_prefix" ]]; then
    rm -rf "$work/SDL"
    git clone --depth 1 --branch "release-$sdl_version" https://github.com/libsdl-org/SDL "$work/SDL"
    cmake -S "$work/SDL" -B "$work/SDL/build" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_OSX_ARCHITECTURES="$architectures" \
        -DCMAKE_OSX_DEPLOYMENT_TARGET="$deployment_target" \
        -DCMAKE_INSTALL_PREFIX="$sdl_prefix" \
        -DSDL_SHARED=OFF -DSDL_STATIC=ON \
        -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF
    cmake --build "$work/SDL/build" --parallel "$jobs"
    cmake --install "$work/SDL/build"
    rm -rf "$work/SDL"
fi

cmake -S "$repo_root" -B "$work/game" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_ARCHITECTURES="$architectures" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET="$deployment_target" \
    -DCMAKE_PREFIX_PATH="$sdl_prefix"
cmake --build "$work/game" --parallel "$jobs"
ctest --test-dir "$work/game" --output-on-failure

# The bundle: the executable and its Info.plist. The shaders are built in, so
# there are no resources to copy.
rm -rf "$app"
mkdir -p "$app/Contents/MacOS"
strip -o "$app/Contents/MacOS/tetris" "$work/game/tetris"
cat > "$app/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleDevelopmentRegion</key>
    <string>en</string>
    <key>CFBundleDisplayName</key>
    <string>Tetris</string>
    <key>CFBundleExecutable</key>
    <string>tetris</string>
    <key>CFBundleIdentifier</key>
    <string>$bundle_id</string>
    <key>CFBundleInfoDictionaryVersion</key>
    <string>6.0</string>
    <key>CFBundleName</key>
    <string>Tetris</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleShortVersionString</key>
    <string>$version</string>
    <key>CFBundleVersion</key>
    <string>$version</string>
    <key>LSApplicationCategoryType</key>
    <string>public.app-category.puzzle-games</string>
    <key>LSMinimumSystemVersion</key>
    <string>$deployment_target</string>
    <key>NSHighResolutionCapable</key>
    <true/>
</dict>
</plist>
EOF
plutil -lint "$app/Contents/Info.plist"

# Stripping drops the linker's ad hoc signature, and Apple silicon runs no
# unsigned code, so the whole bundle is signed again.
codesign --force --sign - "$app"
codesign --verify --strict --verbose=2 "$app"

# Both slices, and nothing loaded from outside macOS itself: a library from
# Homebrew would be missing on the player's Mac. These fail the build, since
# CI logs are easy to skip.
exe="$app/Contents/MacOS/tetris"
archs="$(lipo -archs "$exe")"
lipo "$exe" -verify_arch arm64 x86_64 || fail "Tetris.app is not universal, it has only: $archs"
echo
echo "Libraries it loads:"
otool -L "$exe"
# otool lists each architecture under a header line; the libraries are the
# indented lines below them.
foreign="$(otool -L "$exe" | grep -E '^[[:space:]]' | grep -vE '^[[:space:]]+(/usr/lib/|/System/Library/)' || true)"
if [[ -n "$foreign" ]]; then
    fail "Tetris.app loads libraries that are not part of macOS: $(tr -s '[:space:]' ' ' <<< "$foreign")"
fi

staging="$work/dmg"
rm -rf "$staging"
mkdir -p "$staging" "$repo_root/dist"
cp -R "$app" "$staging/"
ln -s /Applications "$staging/Applications"
# hdiutil sometimes fails with "Resource busy" on CI machines; a retry passes.
for attempt in 1 2 3; do
    if hdiutil create -volname Tetris -srcfolder "$staging" -fs HFS+ -format UDZO -ov "$output"; then
        break
    fi
    if [[ $attempt -eq 3 ]]; then
        fail "hdiutil could not create $output"
    fi
    sleep 5
done
rm -rf "$staging"

notice "Built tetris-macos.dmg: Tetris.app $version for $archs, signed ad hoc, loading only macOS libraries"
