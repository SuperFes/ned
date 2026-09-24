{ pkgs, lib }:
let
  version = "1.0";
  src = pkgs.fetchurl {
    url = "https://example.org/x-${version}.tar.gz";
    hash = lib.fakeHash;
  };
in
pkgs.stdenv.mkDerivation {
  pname = "x";
  inherit version src;
  buildInputs = [
    pkgs.zlib
  ];
}
