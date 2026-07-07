{ stdenv
, cmake
, ninja
, src ? null
, libaribcaption-vendor ? null
, tsduck-vendor ? null
, catch2-vendor ? null
, returnNativeBuildInputs ? false
}:

let
  nativeBuildInputs = [
    cmake
    ninja
  ];
in
if returnNativeBuildInputs then
  nativeBuildInputs
else
  assert src != null;
  assert libaribcaption-vendor != null;
  assert tsduck-vendor != null;
  assert catch2-vendor != null;
  stdenv.mkDerivation {
    pname = "aribcap-dump";
    version = "0.1.0";

    inherit src nativeBuildInputs;

    buildInputs = [ libaribcaption-vendor tsduck-vendor catch2-vendor ];

    cmakeFlags = [
      "-DARIBCAP_ARIBCAPTION_ROOT=${libaribcaption-vendor}"
      "-DARIBCAP_TSDUCK_ROOT=${tsduck-vendor}/usr"
      "-DARIBCAP_CATCH2_ROOT=${catch2-vendor}"
    ];

    doCheck = true;
    ctestFlags = [ "--output-on-failure" ];
  }
