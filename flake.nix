{
  description = "Development environment for the A-10 Cuba! Linux port";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs, ... }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" ];
      forAllSystems = f: nixpkgs.lib.genAttrs systems (system: f nixpkgs.legacyPackages.${system});
    in {
      packages.x86_64-linux.a10-cube-wine = let
        pkgs = nixpkgs.legacyPackages.x86_64-linux;
      in pkgs.writeShellApplication {
        name = "a10-cube-wine";
        runtimeInputs = with pkgs; [ coreutils p7zip util-linux wineWow64Packages.stable ];
        text = ''
          export A10_CUBA_SOUND_MAP=${pkgs.lib.escapeShellArg (toString ./share/sound-events.txt)}
          ${builtins.readFile ./scripts/a10-cube-wine.sh}
        '';
      };
      apps.x86_64-linux.a10-cube-wine = {
        type = "app";
        program = "${self.packages.x86_64-linux.a10-cube-wine}/bin/a10-cube-wine";
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
          ];
        };
      });
    };
}
