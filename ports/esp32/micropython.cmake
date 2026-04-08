get_filename_component(OMV_TOP_DIR "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(MICROPY_MANIFEST_OMV_LIB_DIR "${OMV_TOP_DIR}/scripts/libraries")
set(OMV_ESP32_PORT_MAIN "${OMV_TOP_DIR}/ports/esp32/main.c")
list(APPEND OMV_ESP32_EXTRA_SOURCES
    "${OMV_TOP_DIR}/common/mp_utils.c"
    "${OMV_TOP_DIR}/common/tinyusb_debug.c"
    "${OMV_TOP_DIR}/common/usbdbg.c"
    "${OMV_TOP_DIR}/ports/esp32/omv_debug.c"
)

add_library(usermod_openmv_esp32 INTERFACE)
target_sources(usermod_openmv_esp32 INTERFACE
    ${OMV_TOP_DIR}/modules/py_clock.c
    ${OMV_ESP32_EXTRA_SOURCES}
)
target_include_directories(usermod_openmv_esp32 INTERFACE
    ${OMV_TOP_DIR}
    ${OMV_TOP_DIR}/lib/imlib
    ${OMV_TOP_DIR}/common
    ${OMV_TOP_DIR}/modules
    ${OMV_TOP_DIR}/ports/esp32
    ${OMV_TOP_DIR}/boards/${MICROPY_BOARD}
)
target_link_libraries(usermod INTERFACE usermod_openmv_esp32)
