with import <nixpkgs> {};
with pkgs.python36Packages;

{
py27 = with pkgs.python27Packages; buildPythonPackage {
  name = "supervise_api";
  src = ./.;
  checkInputs = [ utillinux ];
  propagatedBuildInputs = [ (import ../c) linuxfd whichcraft ];
};
py36 = with pkgs.python36Packages; buildPythonPackage {
  name = "supervise_api";
  src = ./.;
  checkInputs = [ utillinux ];
  propagatedBuildInputs = [ (import ../c) linuxfd sfork ];
};
}
