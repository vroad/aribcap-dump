{
  inputs = {
    self.submodules = true;
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs { inherit system; };
      in
      {
        packages =
          let
            libaribcaption-vendor = pkgs.callPackage ./pkgs/libaribcaption-vendor {
              src = self;
            };
            tsduck-vendor = pkgs.callPackage ./pkgs/tsduck-vendor {
              src = self;
            };
            catch2-vendor = pkgs.callPackage ./pkgs/catch2-vendor {
              src = self;
            };
            aribcap-dump = pkgs.callPackage ./pkgs/aribcap-dump {
              src = self;
              inherit libaribcaption-vendor tsduck-vendor catch2-vendor;
            };
          in
          {
            inherit aribcap-dump libaribcaption-vendor tsduck-vendor catch2-vendor;
            default = aribcap-dump;
          };

        devShells.default =
          let
            libaribcaption-vendor-nativeBuildInputs =
              pkgs.callPackage ./pkgs/libaribcaption-vendor {
                returnNativeBuildInputs = true;
              };
            tsduck-vendor-nativeBuildInputs = pkgs.callPackage ./pkgs/tsduck-vendor {
              returnNativeBuildInputs = true;
            };
            catch2-vendor-nativeBuildInputs = pkgs.callPackage ./pkgs/catch2-vendor {
              returnNativeBuildInputs = true;
            };
            aribcap-dump-nativeBuildInputs = pkgs.callPackage ./pkgs/aribcap-dump {
              returnNativeBuildInputs = true;
            };
            packageNativeBuildInputs = pkgs.lib.unique (
              aribcap-dump-nativeBuildInputs
              ++ libaribcaption-vendor-nativeBuildInputs
              ++ tsduck-vendor-nativeBuildInputs
              ++ catch2-vendor-nativeBuildInputs
            );
          in
          pkgs.mkShell {
            hardeningDisable = [ "fortify" ];

            nativeBuildInputs = packageNativeBuildInputs ++ (with pkgs; [
              nixpkgs-fmt
              pre-commit
              clang-tools
              gdb
            ]);
          };
      });
}
