{ lib
, stdenv
, cmake
, src ? null
, returnNativeBuildInputs ? false
}:

let
  nativeBuildInputs = [ cmake ];

  catch2VendorSource = lib.cleanSourceWith {
    inherit src;
    filter = path: type:
      let
        rel = lib.removePrefix "${toString src}/" (toString path);
      in
      rel == "vendor"
      || rel == "vendor/Catch2"
      || lib.hasPrefix "vendor/Catch2/" rel;
  };
in
if returnNativeBuildInputs then
  nativeBuildInputs
else
  assert src != null;
  stdenv.mkDerivation {
    pname = "catch2-vendor";
    version = "unstable";

    src = catch2VendorSource;

    # Catch2's .pc.in templates concatenate ${prefix}/@lib_dir@, but nixpkgs' cmake setup
    # hook passes CMAKE_INSTALL_LIBDIR as an already-absolute path, producing a doubled
    # "${prefix}//nix/store/...". nixpkgs' own catch2_3 package works around this the same
    # way (see pkgs/by-name/ca/catch2_3/package.nix upstream).
    postPatch = ''
      substituteInPlace vendor/Catch2/CMake/*.pc.in \
        --replace-fail '${"$"}{prefix}/' ""
    '';

    # Points cmake's out-of-tree "build/" dir (created by the generic cmake configurePhase)
    # at vendor/Catch2, since that's where Catch2's own CMakeLists.txt lives in this source
    # tree.
    cmakeDir = "../vendor/Catch2";

    inherit nativeBuildInputs;

    # CATCH_INSTALL_EXTRAS is left at its default (ON): ARIBCAP_CATCH2_ROOT locates this
    # package via find_package(), which needs the installed Catch.cmake module alongside
    # the package config.
    cmakeFlags = [
      "-DCATCH_INSTALL_DOCS=OFF"
    ];
  }
