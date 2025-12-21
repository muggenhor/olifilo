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
      # For esptool (https://github.com/mirrexagon/nixpkgs-esp-dev/issues/109)
      config.permittedInsecurePackages = [
        "python3.12-ecdsa-0.19.1"
        "python3.13-ecdsa-0.19.1"
      ];
      overlays = [
        esp-idf-dev.overlays.default
        esp-qemu.overlays.default
      ];
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
      suffix ? "",
      compiler ? gcc,
      toolchain ? [],
    }: pkgs.stdenv.mkDerivation {
      pname = "olifilo${suffix}";
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
    idf-targets = [ "esp32" "esp32c2" "esp32c3" "esp32s2" "esp32s3" "esp32c6" "esp32h2" ];
    idf-arch = {
      esp32   = "xtensa";
      esp32s2 = "xtensa";
      esp32s3 = "xtensa";
      esp32c2 = "riscv";
      esp32c3 = "riscv";
      esp32c6 = "riscv";
      esp32h2 = "riscv";
      esp32p4 = "riscv";
    };
    idf-qemu = with pkgs; {
      esp32   = qemu-esp32;
      esp32s3 = qemu-esp32;
      esp32c3 = qemu-esp32c3;
    };

  in rec {
    packages = rec {
      inherit olifilo;
      default = olifilo;
      inherit (pkgs) qemu-espressif qemu-esp32 qemu-esp32c3;
      qemu-esp32s3 = qemu-esp32;
    } // builtins.listToAttrs (
      map (idf-target: {
        name = "olifilo-${idf-target}";
        value = (idf-olifilo.override rec {
          suffix = "-${idf-target}";
          compiler = pkgs."esp-idf-${idf-arch.${idf-target}}";
          toolchain = [
            "-DCMAKE_TOOLCHAIN_FILE=${compiler}/tools/cmake/toolchain-${idf-target}.cmake"
            "-DIDF_TARGET=${idf-target}"
          ];
        }).overrideAttrs (oldAttrs: {
          prePatch = oldAttrs.prePatch + pkgs.lib.optionalString ((idf-target == "esp32s3") || (idf-target == "esp32c3")) ''
            echo 'CONFIG_ETH_USE_OPENETH=y' >> sdkconfig.defaults
          '';
        });
      }) idf-targets
    );

    images = builtins.listToAttrs (
      map (idf-target: rec {
        name = "olifilo-${idf-target}";
        value =
          with builtins; with pkgs.lib;
          let
            esptool = escapeShellArg (getExe pkgs.esptool);
            pkg = packages."${name}";
            flasher_args = fromJSON (readFile "${pkg}/libexec/olifilo/flasher_args.json");
            flash_chip = escapeShellArg flasher_args.extra_esptool_args.chip;
            flash_size = escapeShellArg flasher_args.flash_settings.flash_size;
            flash_items = concatMap (x: x)
              (map (addr:
                [addr ("${pkg}/libexec/olifilo/" + (baseNameOf flasher_args.flash_files."${addr}"))])
                (attrNames flasher_args.flash_files));
          in pkgs.runCommand "${name}.img" {} ''
            set -x
            ${esptool} --chip ${flash_chip} merge_bin -o "$out" --fill-flash-size ${flash_size} ${escapeShellArgs flasher_args.write_flash_args} ${escapeShellArgs flash_items}
            ${esptool} image_info --version 2 "$out"
            set +x
          '';
      }) idf-targets
    );

    checks = builtins.listToAttrs (
      map (chip: {
        name = "${chip}-qemu";
        value =
        with builtins; with pkgs.lib;
        let
          image = images."olifilo-${chip}";
          qemu = escapeShellArg (getExe idf-qemu.${chip});
          netcat = escapeShellArg (getExe pkgs.netcat);
        in pkgs.runCommand "olifilo-${chip}-qemu.log" {} ''
          set -x
          # copy to get read/write image
          install -m 644 ${image} run.img
          ${qemu} \
            -machine ${chip} \
            -m 2M \
            -nographic \
            -monitor unix:monitor.sock,server,nowait \
            -drive file=run.img,if=mtd,format=raw \
            -serial "file:$out" \
            &
          sleep 3
          echo 'quit' | ${netcat} -N -U monitor.sock
          wait
          cat "$out"
          if grep -q '^Backtrace' "$out"; then
            exit 1
          fi
          grep 'ESP-ROM:${chip}-20210327' "$out"
          grep 'Calling app_main()' "$out"
          set +x
        '';
      }) [ "esp32s3" "esp32c3" ]
    );
  });
}
