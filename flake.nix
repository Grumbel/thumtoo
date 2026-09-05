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
    in {
      packages = forAllSystems ({ pkgs }: {
        default = pkgs.stdenv.mkDerivation {
          pname = "thumtoo";
          version = "0.1.0";
          src = self;
          nativeBuildInputs = [ pkgs.cmake pkgs.ninja pkgs.pkg-config ];
          buildInputs = with pkgs; [
            vips
            libjxl
            glib
            # glib.pc may require sysprof-capture-4.pc at configure time.
            libsysprof-capture
          ];
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
            vips
            libjxl
            glib
            libsysprof-capture
            # Later: ffmpeg libarchive
          ];
          shellHook = ''
            echo "thumtoo dev shell (vips + libjxl)"
            echo "  cmake -B build && cmake --build build && ctest --test-dir build"
          '';
        };
      });
    };
}
