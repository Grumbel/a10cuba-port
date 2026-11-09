{
  description = "Development environment for the A-10 Cuba! Linux port";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs, ... }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" ];
      forAllSystems = f: nixpkgs.lib.genAttrs systems (system: f nixpkgs.legacyPackages.${system});
    in {
      packages.x86_64-linux.a10cuba-wine = let
        pkgs = nixpkgs.legacyPackages.x86_64-linux;
      in pkgs.writeShellApplication {
        name = "a10cuba-wine";
        runtimeInputs = with pkgs; [ coreutils p7zip util-linux wineWow64Packages.stable ];
        text = ''
          export A10_CUBA_SOUND_MAP=${pkgs.lib.escapeShellArg (toString ./share/sound-events.txt)}
          ${builtins.readFile ./scripts/a10cuba-wine.sh}
        '';
      };
      apps.x86_64-linux.a10cuba-wine = {
        type = "app";
        program = "${self.packages.x86_64-linux.a10cuba-wine}/bin/a10cuba-wine";
      };
      packages.x86_64-linux.a10cuba-native = let
        pkgs = nixpkgs.legacyPackages.x86_64-linux;
      in pkgs.stdenv.mkDerivation {
        pname = "a10cuba-native";
        version = "0.1.0";
        src = self;
        dontConfigure = true;
        hardeningDisable = [ "all" ]; # Freestanding, static Linux/i386 ELF.
        buildPhase = ''
          runHook preBuild
          sh ./scripts/build-native-pe32.sh ./a10-pe32-native
          runHook postBuild
        '';
        installPhase = ''
          runHook preInstall
          install -Dm755 ./a10-pe32-native "$out/bin/a10-pe32-native"
          runHook postInstall
        '';
        meta = {
          description = "Experimental freestanding Linux/i386 PE32 mapping and import inspection bootstrap";
          platforms = [ "x86_64-linux" ];
        };
      };
      apps.x86_64-linux.a10cuba-native = let
        pkgs = nixpkgs.legacyPackages.x86_64-linux;
        runner = pkgs.writeShellApplication {
          name = "a10cuba-native";
          text = ''
            export A10_PE32_NATIVE_BIN=${self.packages.x86_64-linux.a10cuba-native}/bin/a10-pe32-native
            ${builtins.readFile ./scripts/a10cuba-native.sh}
          '';
        };
      in {
        type = "app";
        program = "${runner}/bin/a10cuba-native";
        meta.description = "Inspect original A-10 Cuba PE32 modules with the native i386 bootstrap";
      };
      devShells = forAllSystems (pkgs: {
        default = pkgs.mkShell {
          packages = with pkgs; [
            sdl3
            binutils
            cmake
            file
            gcc
            gdb
            ninja
            p7zip
            pkg-config
            python3
            zstd
          ];
        };
      });
    };
}
