{
  description = "HyprExpo Hyprland plugin";

  inputs = {
    hyprland.url = "github:hyprwm/Hyprland/v0.55.2";
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
      inherit (self.packages.${system}) hyprexpo;
    in {
      inherit hyprexpo;

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

      hyprpm-manifest =
        pkgs.runCommand "hyprexpo-hyprpm-manifest-check"
        {
          src = lib.cleanSource ./.;
          nativeBuildInputs =
            [
              pkgs.gnumake
              pkgs.python3
              hyprexpo.stdenv.cc
            ]
            ++ hyprexpo.nativeBuildInputs
            ++ hyprexpo.buildInputs;
        }
        ''
          cp -r "$src" src
          chmod -R u+w src
          cd src

          python - <<'PY'
          import tomllib
          from pathlib import Path

          manifest = tomllib.loads(Path("hyprpm.toml").read_text())

          repository = manifest["repository"]
          assert repository["name"] == "hyprexpo"
          assert repository["authors"]

          plugin = manifest["hyprexpo"]
          assert plugin["output"] == "hyprexpo.so"
          assert plugin["build"] == ["make all"]
          assert plugin["authors"]
          PY

          make all
          test -f hyprexpo.so

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
