{
  description = "Development environment for the A-10 Cuba! Linux port";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { nixpkgs, ... }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" ];
      forAllSystems = f: nixpkgs.lib.genAttrs systems (system: f nixpkgs.legacyPackages.${system});
    in {
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
