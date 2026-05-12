{
  description = "HyprExpo Hyprland plugin";

  inputs = {
    hyprland.url = "github:hyprwm/Hyprland/v0.55.0";
    nixpkgs.follows = "hyprland/nixpkgs";
    systems.follows = "hyprland/systems";
  };

  outputs = {
    self,
    hyprland,
    nixpkgs,
    systems,
    ...
  }: let
    inherit (nixpkgs) lib;
    eachSystem = lib.genAttrs (import systems);

    pkgsFor = eachSystem (system:
      import nixpkgs {
        localSystem.system = system;
        overlays = [
          hyprland.overlays.hyprland-packages
          self.overlays.hyprland-clang
          self.overlays.default
        ];
      });
  in {
    overlays = {
      default = final: prev: {
        hyprlandPlugins =
          (prev.hyprlandPlugins or {})
          // {
            hyprexpo = final.callPackage ./default.nix {};
          };

        inherit (final.hyprlandPlugins) hyprexpo;
      };

      hyprland-clang = final: prev: {
        hyprland = prev.hyprland.override {
          stdenv = final.clangStdenv;
        };

        hyprland-unwrapped = final.hyprland.override {
          wrapRuntimeDeps = false;
        };

        hyprland-with-tests = final.hyprland.override {
          withTests = true;
        };
      };
    };

    packages = eachSystem (system: {
      default = self.packages.${system}.hyprexpo;
      inherit (pkgsFor.${system}.hyprlandPlugins) hyprexpo;
    });

    checks = eachSystem (system: let
      pkgs = pkgsFor.${system};
    in {
      inherit (self.packages.${system}) hyprexpo;

      format =
        pkgs.runCommand "hyprexpo-format-check"
        {
          src = lib.cleanSource ./.;
          nativeBuildInputs = with pkgs; [
            alejandra
            clang-tools
            deadnix
            statix
          ];
        }
        ''
          cp -r "$src" src
          chmod -R u+w src
          cd src

          clang-format --dry-run --Werror *.cpp *.hpp
          alejandra --check *.nix
          deadnix --fail .
          statix check .

          touch "$out"
        '';
    });

    devShells = eachSystem (system: let
      pkgs = pkgsFor.${system};
    in {
      default =
        pkgs.mkShell.override {
          inherit (self.packages.${system}.hyprexpo) stdenv;
        } {
          name = "hyprexpo";

          inputsFrom = [
            self.packages.${system}.hyprexpo
            hyprland.packages.${system}.hyprland
          ];

          packages = with pkgs; [
            alejandra
            clang-tools
            cmake
            deadnix
            meson
            ninja
            pkg-config
            statix
          ];
        };
    });

    formatter = eachSystem (system: pkgsFor.${system}.alejandra);
  };
}
