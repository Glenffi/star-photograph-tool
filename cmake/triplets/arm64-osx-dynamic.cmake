# StarProcessor links LibRaw and libtiff through concrete library paths, so the
# macOS package uses dynamic vcpkg libraries that macdeployqt can discover and
# relocate into the app bundle. The stock arm64-osx triplet is static.
set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE dynamic)
set(VCPKG_CMAKE_SYSTEM_NAME Darwin)
set(VCPKG_OSX_ARCHITECTURES arm64)
set(VCPKG_OSX_DEPLOYMENT_TARGET 14.0)
