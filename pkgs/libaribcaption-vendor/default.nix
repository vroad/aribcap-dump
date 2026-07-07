{ lib
, stdenv
, cmake
, src ? null
, returnNativeBuildInputs ? false
}:

let
  nativeBuildInputs = [ cmake ];

  libaribcaptionVendorSource = lib.cleanSourceWith {
    inherit src;
    filter = path: type:
      let
        rel = lib.removePrefix "${toString src}/" (toString path);
      in
      rel == "vendor"
      || rel == "vendor/libaribcaption"
      || lib.hasPrefix "vendor/libaribcaption/" rel;
  };
in
if returnNativeBuildInputs then
  nativeBuildInputs
else
  assert src != null;
  stdenv.mkDerivation {
    pname = "libaribcaption-vendor";
    version = "unstable";

    src = libaribcaptionVendorSource;

    cmakeDir = "../vendor/libaribcaption";

    inherit nativeBuildInputs;

    cmakeFlags = [
      "-DARIBCC_NO_RENDERER=ON"
      "-DARIBCC_BUILD_TESTS=OFF"
      "-DARIBCC_SHARED_LIBRARY=OFF"
    ];
  }
