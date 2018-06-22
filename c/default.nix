with import <nixpkgs> {};

stdenv.mkDerivation {
  name = "supervise";
  src = ./.;
  buildInputs = [ autoconf automake pkgconfig autoreconfHook ];
}
