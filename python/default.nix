with import <nixpkgs> {};
with pkgs.python36Packages;
buildPythonPackage {
  name = "supervise_api";
  src = ./.;
  checkInputs = [ utillinux ];
  buildInputs = [ pkgconfig ];
  propagatedBuildInputs = [ (import ../c) sfork cffi dataclasses python-prctl ];
}
