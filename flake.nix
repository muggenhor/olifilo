# SPDX-License-Identifier: GPL-3.0-or-later

{
  inputs.nixpkgs.url = "github:NixOS/nixpkgs";
  inputs.flake-utils.url = "github:numtide/flake-utils";
  inputs.esp-idf-dev = {
    url = "github:mirrexagon/nixpkgs-esp-dev";
    inputs.nixpkgs.follows = "nixpkgs";
    inputs.flake-utils.follows = "flake-utils";
  };

  outputs = inputs@{ self, esp-idf-dev, flake-utils, nixpkgs, ... }:
  flake-utils.lib.eachDefaultSystem (system:
  let
    versionOf = flake:
      with nixpkgs.lib;
      "${if flake ? lastModifiedDate then (substring 0 8 flake.lastModifiedDate) else "19700101"}${
        optionalString (flake ? revCount) "-${toString flake.revCount}"
      }-${
        if (flake ? shortRev) || (flake ? dirtyShortRev) then "g${flake.shortRev or flake.dirtyShortRev}" else "dirty"
      }";

    pkgs = import nixpkgs {
      inherit system;
    };
    gcc =
      if builtins.compareVersions "14" pkgs.gcc.version <= 0
      then pkgs.gcc
      else pkgs.gcc14;
    llvmPackages =
      if builtins.compareVersions "18" pkgs.llvmPackages.release_version <= 0
      then pkgs.llvmPackages
      else pkgs.llvmPackages_18;
    olifilo = pkgs.callPackage ({
      version ? versionOf self,
      prefix ? "",
      compiler ? gcc,
      toolchain ? [],
    }: pkgs.stdenv.mkDerivation {
      pname = "${prefix}olifilo";
      inherit version;

      src = ./.;

      nativeBuildInputs = [
        pkgs.cmake
        pkgs.ninja
        compiler
      ];

      cmakeFlags = [
        "-DPROJECT_VER=${version}"
      ] ++ toolchain;

      ${if self ? lastModified then "SOURCE_DATE_EPOCH" else null} = self.lastModified;

      cmakeBuildType = "Debug";
      doCheck = true;
    }) {};
    idf-olifilo = olifilo.overrideAttrs {
      prePatch = ''
        cd idf
      '';

      dontAutoPatchelf = true;
      dontPatchELF = true;
      dontStrip = true;
    };
  in {
    packages = {
      inherit olifilo;
      default = olifilo;
    } // builtins.listToAttrs (
      map (idf-target: {
        name = "${idf-target}-olifilo";
        value = idf-olifilo.override rec {
          prefix = "${idf-target}-";
          compiler = esp-idf-dev.packages.${system}."esp-idf-${idf-target}";
          toolchain = [
            "-DCMAKE_TOOLCHAIN_FILE=${compiler}/tools/cmake/toolchain-${idf-target}.cmake"
            "-DIDF_TARGET=${idf-target}"
          ];
        };
      }) [ "esp32" "esp32c2" "esp32c3" "esp32s2" "esp32s3" "esp32c6" "esp32h2" ]
    );
  });
}
