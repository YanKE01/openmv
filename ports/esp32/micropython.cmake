get_filename_component(OMV_TOP_DIR "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(MICROPY_MANIFEST_OMV_LIB_DIR "${OMV_TOP_DIR}/scripts/libraries")
set(MICROPY_SOURCE_PORT_MAIN "../../../../ports/esp32/main.c")

add_library(usermod_openmv_esp32 INTERFACE)
target_sources(usermod_openmv_esp32 INTERFACE
    ${OMV_TOP_DIR}/common/mutex.c
    ${OMV_TOP_DIR}/modules/py_clock.c
    ${OMV_TOP_DIR}/protocol/omv_protocol.c
    ${OMV_TOP_DIR}/protocol/omv_protocol_channel_stdio.c
    ${OMV_TOP_DIR}/protocol/omv_protocol_channel_stream.c
    ${OMV_TOP_DIR}/protocol/omv_protocol_channel_tinyusb.c
    ${OMV_TOP_DIR}/common/omv_crc.c
    ${OMV_TOP_DIR}/lib/imlib/font.c
    ${OMV_TOP_DIR}/lib/imlib/lab_tab.c
    ${OMV_TOP_DIR}/ports/esp32/omv_bar.c
    ${OMV_TOP_DIR}/ports/esp32/omv_qr.c
    ${OMV_TOP_DIR}/ports/esp32/omv_imlib_blob_min.c
    ${OMV_TOP_DIR}/ports/esp32/omv_imlib_filter_min.c
    ${OMV_TOP_DIR}/ports/esp32/omv_imlib_gray_min.c
    ${OMV_TOP_DIR}/ports/esp32/omv_imlib_edge_min.c
    ${OMV_TOP_DIR}/ports/esp32/omv_imlib_shape_min.c
    ${OMV_TOP_DIR}/ports/esp32/omv_imlib_draw_min.c
    ${OMV_TOP_DIR}/ports/esp32/py_image_lite.c
    ${OMV_TOP_DIR}/ports/esp32/py_sensor.c
    ${OMV_TOP_DIR}/ports/esp32/omv_gpio.c
    ${OMV_TOP_DIR}/ports/esp32/omv_camera.c
    ${OMV_TOP_DIR}/ports/esp32/omv_framebuffer.c
    ${OMV_TOP_DIR}/ports/esp32/omv_test_preview.c
)
target_include_directories(usermod_openmv_esp32 INTERFACE
    ${OMV_TOP_DIR}
    ${OMV_TOP_DIR}/common
    ${OMV_TOP_DIR}/lib/imlib
    ${OMV_TOP_DIR}/protocol
    ${OMV_TOP_DIR}/modules
    ${OMV_TOP_DIR}/ports/esp32
    ${OMV_TOP_DIR}/boards/${MICROPY_BOARD}
)

target_compile_definitions(usermod_openmv_esp32 INTERFACE
    MICROPY_OPENMV=1
    OMV_USB_STACK_TINYUSB=1
    OMV_USB_VID=0x37C5
    OMV_USB_PID=0x1204
    OMV_PROTOCOL_DEFAULT_CHANNELS=0
    OMV_PROTOCOL_HAS_FRAMEBUFFER=1
)

target_link_options(usermod_openmv_esp32 INTERFACE
    -Wl,--wrap=mp_hal_stdio_poll
    -Wl,--wrap=mp_hal_stdout_tx_strn
)

target_link_libraries(usermod INTERFACE usermod_openmv_esp32)
