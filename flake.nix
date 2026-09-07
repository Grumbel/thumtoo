# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later
{
  description = "thumtoo — media index and display-pixel ladder library";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" "x86_64-darwin" "aarch64-darwin" ];
      forAllSystems = f: nixpkgs.lib.genAttrs systems (system: f {
        inherit system;
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
        libsepol       # libsepol.pc (Requires.private of libselinux)

        # libarchive Requires.private
        libarchive
        poppler
        curl
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
        libthai        # libthai.pc (pango)
        libdatrie      # libdatrie.pc (libthai)
        libtiff
        librsvg
        libxml2        # libxml-2.0.pc (librsvg / others)
        dav1d
        matio
        hdf5
        lcms2
        openexr
        libraw
        openjpeg
        libhwy
        libxdmcp       # xdmcp.pc (X11 transitive via pango/cairo)
      ];

      mkPackage = pkgs: pkgs.stdenv.mkDerivation {
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

      # Galapix-style out-of-tree helpers for `nix develop`.
      mkDevScripts = pkgs:
        let
          preamble = ''
            set -euo pipefail
            if [ -z "''${THUMTOO_SOURCE:-}" ]; then
              echo "$0: THUMTOO_SOURCE is not set (enter the shell with: nix develop)" >&2
              exit 1
            fi
            THUMTOO_BUILD_DIR="''${THUMTOO_BUILD_DIR:-/tmp/thumtoo-build}"
          '';
          configure = pkgs.writeShellScriptBin "thumtoo-configure" (
            preamble
            + ''
              cmake -S "$THUMTOO_SOURCE" -B "$THUMTOO_BUILD_DIR" -G Ninja \
                -DCMAKE_BUILD_TYPE="''${CMAKE_BUILD_TYPE:-Debug}" \
                -DTHUMTOO_BUILD_TESTS=ON \
                -DTHUMTOO_BUILD_TOOLS=ON
            ''
          );
          build = pkgs.writeShellScriptBin "thumtoo-build" (
            preamble
            + ''
              if [ ! -f "$THUMTOO_BUILD_DIR/build.ninja" ] && [ ! -f "$THUMTOO_BUILD_DIR/Makefile" ]; then
                thumtoo-configure || exit 1
              fi
              cmake --build "$THUMTOO_BUILD_DIR" "$@"
            ''
          );
          test = pkgs.writeShellScriptBin "thumtoo-test" (
            preamble
            + ''
              thumtoo-build || exit 1
              ctest --test-dir "$THUMTOO_BUILD_DIR" --output-on-failure "$@"
            ''
          );
          runTool = name: pkgs.writeShellScriptBin "thumtoo-run-${name}" (
            preamble
            + ''
              thumtoo-build || exit 1
              bin="$THUMTOO_BUILD_DIR/thumtoo-${name}"
              if [ ! -x "$bin" ]; then
                echo "thumtoo-run-${name}: $bin missing after build" >&2
                exit 1
              fi
              # No exec: return to interactive shell when typed by hand.
              "$bin" "$@"
            ''
          );
          runStatus = runTool "status";
          runPrepare = runTool "prepare";
          runBench = runTool "bench";
          runGdb = pkgs.writeShellScriptBin "thumtoo-run-gdb" (
            preamble
            + ''
              thumtoo-build || exit 1
              tool="''${1:-prepare}"
              shift || true
              bin="$THUMTOO_BUILD_DIR/thumtoo-$tool"
              if [ ! -x "$bin" ]; then
                echo "thumtoo-run-gdb: $bin missing after build" >&2
                exit 1
              fi
              if ! command -v gdb >/dev/null 2>&1; then
                echo "thumtoo-run-gdb: gdb not found (should be in the nix develop shell)" >&2
                exit 1
              fi
              gdb --args "$bin" "$@"
            ''
          );
        in [
          configure
          build
          test
          runStatus
          runPrepare
          runBench
          runGdb
        ];
    in {
      packages = forAllSystems ({ pkgs, ... }: {
        default = mkPackage pkgs;
      });

      apps = forAllSystems ({ pkgs, system, ... }:
        let
          pkg = self.packages.${system}.default;
          app = exe: {
            type = "app";
            program = "${pkg}/bin/${exe}";
          };
        in {
          default = app "thumtoo-status";
          status = app "thumtoo-status";
          prepare = app "thumtoo-prepare";
          bench = app "thumtoo-bench";
        });

      devShells = forAllSystems ({ pkgs, ... }: {
        default = pkgs.mkShell {
          inputsFrom = [ (mkPackage pkgs) ];
          packages = with pkgs; [
            cmake
            ninja
            gcc
            clang-tools
            gdb
            pkg-config
          ] ++ (vipsInputs pkgs) ++ (mkDevScripts pkgs);
          CMAKE_BUILD_TYPE = "Debug";
          shellHook = ''
            export THUMTOO_SOURCE="$PWD"
            export THUMTOO_BUILD_DIR="''${THUMTOO_BUILD_DIR:-/tmp/thumtoo-build}"
            echo "thumtoo dev shell (CMAKE_BUILD_TYPE=''${CMAKE_BUILD_TYPE:-Debug})"
            echo "  source:    $THUMTOO_SOURCE"
            echo "  build dir: $THUMTOO_BUILD_DIR"
            echo "  thumtoo-configure          # cmake -S . -B \$THUMTOO_BUILD_DIR -G Ninja"
            echo "  thumtoo-build [args…]      # build (configure if needed)"
            echo "  thumtoo-test [ctest args…] # build + ctest"
            echo "  thumtoo-run-status …"
            echo "  thumtoo-run-prepare …"
            echo "  thumtoo-run-bench …"
            echo "  thumtoo-run-gdb [tool] …   # tool = prepare|bench|status (default: prepare)"
            echo "  flake apps: nix run .#status|prepare|bench"
          '';
        };
      });
    };
}
