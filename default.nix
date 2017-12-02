with import <nixpkgs> {};

stdenv.mkDerivation {
  name = "supervise";
  buildInputs = [ autoconf automake ];
}
