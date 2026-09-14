# A representative derivation: let/in, attrsets, functions, interpolation.
{ pkgs ? import <nixpkgs> {} }:

let
  version = "1.0.0";
  name = "sample";
in
pkgs.stdenv.mkDerivation rec {
  inherit name version;

  src = pkgs.fetchurl {
    url = "https://example.com/${name}-${version}.tar.gz";
    sha256 = "0000000000000000000000000000000000000000000000";
  };

  buildInputs = [ pkgs.gcc pkgs.gnumake ];

  meta = with pkgs.lib; {
    description = "A sample package";
    license = licenses.mit;
  };
}
