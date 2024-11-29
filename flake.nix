# SPDX-License-Identifier: GPL-3.0-or-later

{
  inputs.nixpkgs.url = "github:NixOS/nixpkgs";
  inputs.flake-utils.url = "github:numtide/flake-utils";

  outputs = inputs@{ self, flake-utils, nixpkgs, ... }:
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
    olifilo = let
      version = versionOf self;
    in pkgs.stdenv.mkDerivation {
      pname = "olifilo";
      inherit version;

      src = ./.;

      nativeBuildInputs = [
        pkgs.cmake
        pkgs.ninja
        gcc
      ];

      cmakeFlags = [
        "-DPROJECT_VER=${version}"
      ];

      cmakeBuildType = "Debug";
      doCheck = true;
    };
  in {
    packages = {
      inherit olifilo;
      default = olifilo;
    };
  });
}
