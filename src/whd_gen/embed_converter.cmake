# SPDX-License-Identifier: GPL-2.0-or-later
file(READ "${PROGRAM}" converter_program)
file(READ "${INSTALLER}" converter_installer)
if (converter_program MATCHES "</[sS][cC][rR][iI][pP][tT]" OR converter_installer MATCHES "</[sS][cC][rR][iI][pP][tT]")
    message(FATAL_ERROR "Converter JavaScript cannot be embedded in an HTML script element")
endif()
execute_process(
    COMMAND "${PYTHON}" -c "import base64, pathlib, sys; print(base64.b64encode(pathlib.Path(sys.argv[1]).read_bytes()).decode('ascii'), end='')" "${FIRMWARE}"
    OUTPUT_VARIABLE converter_firmware
    RESULT_VARIABLE encode_result
)
if (NOT encode_result STREQUAL "0" OR converter_firmware STREQUAL "")
    message(FATAL_ERROR "Could not embed the Badger firmware UF2")
endif()
set(converter_licenses "")
foreach (license IN LISTS LICENSES)
    file(READ "${license}" text)
    string(APPEND converter_licenses "${text}\n\n")
endforeach()
string(REPLACE "&" "&amp;" converter_licenses "${converter_licenses}")
string(REPLACE "<" "&lt;" converter_licenses "${converter_licenses}")
configure_file("${TEMPLATE}" "${OUTPUT}" @ONLY)
