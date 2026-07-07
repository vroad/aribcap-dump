{ lib
, stdenv
, bash
, cmake
, gnumake
, python3
, src ? null
, returnNativeBuildInputs ? false
}:

let
  nativeBuildInputs = [
    cmake
    gnumake
    python3
  ];

  tsduckVendorSource = lib.cleanSourceWith {
    inherit src;
    filter = path: type:
      let
        rel = lib.removePrefix "${toString src}/" (toString path);
      in
      rel == "cmake"
      || rel == "cmake/InstallTSDuck.cmake"
      || rel == "vendor"
      || rel == "vendor/tsduck"
      || lib.hasPrefix "vendor/tsduck/" rel;
  };
in
if returnNativeBuildInputs then
  nativeBuildInputs
else
  assert src != null;
  stdenv.mkDerivation {
    pname = "tsduck-vendor";
    version = "unstable";

    src = tsduckVendorSource;

    dontConfigure = true;

    inherit nativeBuildInputs;

    postPatch = ''
      patchShebangs vendor/tsduck/scripts vendor/tsduck/src/utils
      substituteInPlace vendor/tsduck/Makefile.inc \
        --replace-fail \
          'SHELL = /usr/bin/env bash --noprofile --norc $(if $(SHELL_VERBOSE),-x)' \
          'SHELL = ${bash}/bin/bash --noprofile --norc $(if $(SHELL_VERBOSE),-x)'
    '';

    buildPhase = ''
      runHook preBuild
      cmake \
        -DTSDUCK_MAKE_EXECUTABLE=make \
        -DTSDUCK_JOBS="$NIX_BUILD_CORES" \
        -DTSDUCK_SOURCE_DIR="$PWD/vendor/tsduck" \
        -DTSDUCK_INSTALL_ROOT="$out" \
        -DTSDUCK_BUILD_ROOT="$TMPDIR/tsduck-build" \
        -DTSDUCK_CC="$CC" \
        -DTSDUCK_CXX="$CXX" \
        -DTSDUCK_AR="$AR" \
        -P "$PWD/cmake/InstallTSDuck.cmake"
      runHook postBuild
    '';

    installPhase = ''
      runHook preInstall
      runHook postInstall
    '';
  }
