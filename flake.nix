# SPDX-License-Identifier: GPL-3.0-or-later

{
  inputs.nixpkgs.url = "github:NixOS/nixpkgs";
  inputs.flake-utils.url = "github:numtide/flake-utils";
  inputs.esp-idf-dev = {
    url = "github:mirrexagon/nixpkgs-esp-dev";
    inputs.nixpkgs.follows = "nixpkgs";
    inputs.flake-utils.follows = "flake-utils";
  };
  inputs.esp-qemu = {
    url = "github:SFrijters/nix-qemu-espressif";
    inputs.nixpkgs.follows = "nixpkgs";
  };

  outputs = inputs@{ self, esp-idf-dev, esp-qemu, flake-utils, nixpkgs, ... }:
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
  in rec {
    packages = rec {
      inherit olifilo;
      default = olifilo;
      inherit (esp-qemu.packages.${system}) qemu-espressif qemu-esp32 qemu-esp32c3;
      qemu-esp32s3 = qemu-esp32;
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

    checks = {
      esp32s3-qemu = pkgs.runCommand "esp32s3-olifilo-qemu.log" {} ''
        set -x
        ${pkgs.lib.escapeShellArg (pkgs.lib.getExe pkgs.espflash)} save-image --chip esp32s3 --merge ${pkgs.lib.escapeShellArg packages.esp32s3-olifilo}/bin/olifilo.elf --bootloader ${pkgs.lib.escapeShellArg packages.esp32s3-olifilo}/libexec/olifilo/bootloader.bin --flash-size 4mb esp32s3-olifilo.img
        ${pkgs.lib.escapeShellArg (pkgs.lib.getExe pkgs.esptool)} image_info --version 2 esp32s3-olifilo.img
        ${pkgs.lib.escapeShellArg (pkgs.lib.getExe packages.qemu-esp32s3)} -nographic -monitor unix:monitor.sock,server,nowait -machine esp32s3 -drive file=esp32s3-olifilo.img,if=mtd,format=raw -m 2M -serial "file:$out" &
        sleep 3
        echo 'quit' | ${pkgs.lib.escapeShellArg (pkgs.lib.getExe pkgs.netcat)} -N -U monitor.sock
        wait
        cat "$out"
        if grep -q '^Backtrace' "$out"; then
          exit 1
        fi
        grep 'ESP-ROM:esp32s3-20210327' "$out"
        grep 'Calling app_main()' "$out"
        set +x
      '';
    };
  });
}
