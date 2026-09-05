# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later
{
  description = "thumtoo — media index and display-pixel ladder library";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" "x86_64-darwin" "aarch64-darwin" ];
      forAllSystems = f: nixpkgs.lib.genAttrs systems (system: f {
        pkgs = import nixpkgs { inherit system; };
      });

      # libvips + JPEG-XL, and the Requires.private packages whose .pc files
      # pkg-config looks for when probing vips (same set biltoo uses to silence
      # "Package '…' was not found" spam). We do not necessarily link all of
      # these into thumtoo; they only need to be on PKG_CONFIG_PATH.
      vipsInputs = pkgs: with pkgs; [
        sqlite
        vips
        libjxl

        # glib Requires.private
        glib
        libsysprof-capture
        pcre2          # libpcre2-8.pc

        # gio-2.0 Requires.private (pulled in via glib/vips)
        util-linux     # mount.pc
        libselinux     # libselinux.pc

        # libarchive Requires.private
        libarchive
        openssl        # libcrypto.pc

        # vips Requires.private (and common transitive .pc names)
        fftw
        cfitsio
        libimagequant
        cgif
        libexif
        libultrahdr
        libwebp
        pango
        fribidi
        libtiff
        librsvg
        dav1d
        matio
        hdf5
        lcms2
        openexr
        libraw
        openjpeg
        libhwy
      ];
    in {
      packages = forAllSystems ({ pkgs }: {
        default = pkgs.stdenv.mkDerivation {
          pname = "thumtoo";
          version = "0.1.0";
          src = self;
          nativeBuildInputs = [ pkgs.cmake pkgs.ninja pkgs.pkg-config ];
          buildInputs = vipsInputs pkgs;
          cmakeFlags = [
            "-GNinja"
            "-DTHUMTOO_BUILD_TESTS=ON"
            "-DTHUMTOO_BUILD_TOOLS=ON"
          ];
          doCheck = true;
          meta = with pkgs.lib; {
            description = "Persistent media index and display-pixel ladder";
            license = licenses.gpl3Plus;
            platforms = platforms.unix;
          };
        };
      });

      devShells = forAllSystems ({ pkgs }: {
        default = pkgs.mkShell {
          packages = with pkgs; [
            cmake
            ninja
            gcc
            clang-tools
            gdb
            pkg-config
          ] ++ vipsInputs pkgs;
          shellHook = ''
            echo "thumtoo dev shell (vips + libjxl + private .pc deps)"
            echo "  cmake -B build && cmake --build build && ctest --test-dir build"
          '';
        };
      });
    };
}
