{
  description = "CSC255 Password Cracker";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs }:
  let
    system = "x86_64-linux";
    pkgs = nixpkgs.legacyPackages.${system};
  in {
    devShells.${system}.default = pkgs.mkShell {
      packages = with pkgs; [
        gcc
        gnumake
        pkg-config
        openssl
        curl
        python311
        uv
        linuxPackages.perf
        valgrind
        hyperfine
        google-benchmark
        gtest
        quill-log
      ];

      shellHook = ''
        echo "CSC255 dev environment ready"
      '';
    };
  };
}
