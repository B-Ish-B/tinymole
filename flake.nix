{
  description = "Password Cracker";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = nixpkgs.legacyPackages.${system};
        linuxOnly = if pkgs.stdenv.isLinux then [ pkgs.linuxPackages.perf pkgs.valgrind ] else [];
      in {
        devShells.default = pkgs.mkShell {
          packages = with pkgs; [
            gcc
            gnumake
            pkg-config
            openssl
            curl
            python311
            uv
            hyperfine
            lazygit
            delta
            gbenchmark
            gtest
            quill-log
          ] ++ linuxOnly;

          shellHook = ''
            echo "environment ready"
          '';
        };
      }
    );
}
