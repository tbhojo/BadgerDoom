# Modified for Badger Doom (2026-11-24).

if (NOT PICO_ON_DEVICE)
    add_library(wad INTERFACE)
    target_sources(wad INTERFACE
            wad.cpp)
    target_include_directories(wad INTERFACE ${CMAKE_CURRENT_LIST_DIR})

    add_executable(whd_gen
            whd_gen.cpp
            mus2seq.cpp
            huff.cpp
            lodepng.cpp
            compress_mus.cpp
            ../tiny_huff.c
            ../musx_decoder.c
            ../image_decoder.c
            )

    target_compile_definitions(whd_gen PRIVATE IS_WHD_GEN=1)

    target_include_directories(whd_gen PRIVATE . .. ../doom)
    target_link_libraries(whd_gen PRIVATE wad adpcm-lib)

    if (EMSCRIPTEN)
        set(BADGER_FIRMWARE_UF2 "" CACHE FILEPATH "Firmware-only UF2 for the GitHub Universe 2024 Badger")
        if (NOT EXISTS "${BADGER_FIRMWARE_UF2}")
            message(FATAL_ERROR "Set BADGER_FIRMWARE_UF2 to the GitHub Universe Badger doom_tiny.uf2")
        endif()
        find_package(Python3 REQUIRED COMPONENTS Interpreter)
        set_target_properties(whd_gen PROPERTIES SUFFIX ".js")
        target_compile_options(whd_gen PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fexceptions>)
        target_link_options(whd_gen PRIVATE
            -fexceptions
            -sSINGLE_FILE=1
            -sENVIRONMENT=worker
            -sMODULARIZE=1
            -sEXPORT_NAME=createWhdConverter
            -sEXPORTED_RUNTIME_METHODS=FS,callMain
            -sINVOKE_RUN=0
            -sALLOW_MEMORY_GROWTH=1
            -sMAXIMUM_MEMORY=268435456
            -sDYNAMIC_EXECUTION=0
        )

        set(html "${CMAKE_CURRENT_BINARY_DIR}/badger-doom-installer.html")
        set(template "${CMAKE_CURRENT_SOURCE_DIR}/converter.html")
        set(embed "${CMAKE_CURRENT_SOURCE_DIR}/embed_converter.cmake")
        set(worker_scripts
            "${CMAKE_CURRENT_SOURCE_DIR}/installer.js"
            "${CMAKE_CURRENT_SOURCE_DIR}/shareware.js"
        )
        set(licenses
            "${PROJECT_SOURCE_DIR}/COPYING.md"
            "${PROJECT_SOURCE_DIR}/src/adpcm-xq/license.txt"
            "${EMSCRIPTEN_ROOT_PATH}/LICENSE"
            "${EMSCRIPTEN_ROOT_PATH}/system/lib/libc/musl/COPYRIGHT"
            "${EMSCRIPTEN_ROOT_PATH}/system/lib/libcxx/LICENSE.TXT"
            "${EMSCRIPTEN_ROOT_PATH}/system/lib/libcxxabi/LICENSE.TXT"
            "${EMSCRIPTEN_ROOT_PATH}/system/lib/compiler-rt/LICENSE.TXT"
            "${EMSCRIPTEN_ROOT_PATH}/system/lib/libunwind/LICENSE.TXT"
        )
        add_custom_command(OUTPUT "${html}"
            COMMAND "${CMAKE_COMMAND}" "-DPROGRAM=$<TARGET_FILE:whd_gen>"
                "-DSCRIPTS=${worker_scripts}" "-DFIRMWARE=${BADGER_FIRMWARE_UF2}"
                "-DPYTHON=${Python3_EXECUTABLE}" "-DTEMPLATE=${template}"
                "-DOUTPUT=${html}" "-DLICENSES=${licenses}" -P "${embed}"
            DEPENDS whd_gen "${template}" "${embed}" ${worker_scripts} "${BADGER_FIRMWARE_UF2}" ${licenses}
            VERBATIM
        )
        add_custom_target(whd_converter DEPENDS "${html}")
    endif()
endif()
