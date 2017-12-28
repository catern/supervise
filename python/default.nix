with import <nixpkgs> {};
with pkgs.python36Packages;

buildPythonPackage {
  name = "supervise_api";
  src = ./.;
  propagatedBuildInputs = [ (import ./..) linuxfd ];
}
