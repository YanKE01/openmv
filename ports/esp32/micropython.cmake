get_filename_component(OMV_TOP_DIR "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(MICROPY_MANIFEST_OMV_LIB_DIR "${OMV_TOP_DIR}/scripts/libraries")
set(OMV_ESP32_PORT_MAIN "${OMV_TOP_DIR}/ports/esp32/main.c")
list(APPEND MICROPY_DEF_COMPONENT
    CMSIS_MCU_H=\"cmsis_compiler.h\"
    MICROPY_OPENMV=1
    OMV_PY_IMAGE_ESP32_MINIMAL=1
    OMV_USB_STACK_TINYUSB=1
)
list(APPEND OMV_ESP32_EXTRA_SOURCES
    "${OMV_TOP_DIR}/common/mutex.c"
    "${OMV_TOP_DIR}/common/array.c"
    "${OMV_TOP_DIR}/common/mp_utils.c"
    "${OMV_TOP_DIR}/common/tinyusb_debug.c"
    "${OMV_TOP_DIR}/common/unaligned_memcpy.c"
    "${OMV_TOP_DIR}/common/umm_malloc.c"
    "${OMV_TOP_DIR}/common/usbdbg.c"
    "${OMV_TOP_DIR}/lib/imlib/bayer.c"
    "${OMV_TOP_DIR}/lib/imlib/blob.c"
    "${OMV_TOP_DIR}/lib/imlib/binary.c"
    "${OMV_TOP_DIR}/lib/imlib/clahe.c"
    "${OMV_TOP_DIR}/lib/imlib/collections.c"
    "${OMV_TOP_DIR}/lib/imlib/draw.c"
    "${OMV_TOP_DIR}/lib/imlib/edge.c"
    "${OMV_TOP_DIR}/lib/imlib/eye.c"
    "${OMV_TOP_DIR}/lib/imlib/filter.c"
    "${OMV_TOP_DIR}/lib/imlib/fmath.c"
    "${OMV_TOP_DIR}/lib/imlib/font.c"
    "${OMV_TOP_DIR}/lib/imlib/fsort.c"
    "${OMV_TOP_DIR}/lib/imlib/imlib.c"
    "${OMV_TOP_DIR}/lib/imlib/jpegd.c"
    "${OMV_TOP_DIR}/lib/imlib/jpege.c"
    "${OMV_TOP_DIR}/lib/imlib/lab_tab.c"
    "${OMV_TOP_DIR}/lib/imlib/line.c"
    "${OMV_TOP_DIR}/lib/imlib/mathop.c"
    "${OMV_TOP_DIR}/lib/imlib/point.c"
    "${OMV_TOP_DIR}/lib/imlib/png.c"
    "${OMV_TOP_DIR}/lib/imlib/qrcode.c"
    "${OMV_TOP_DIR}/lib/imlib/qsort.c"
    "${OMV_TOP_DIR}/lib/imlib/rainbow_tab.c"
    "${OMV_TOP_DIR}/lib/imlib/rectangle.c"
    "${OMV_TOP_DIR}/lib/imlib/sincos_tab.c"
    "${OMV_TOP_DIR}/lib/imlib/stats.c"
    "${OMV_TOP_DIR}/lib/imlib/xyz_tab.c"
    "${OMV_TOP_DIR}/lib/imlib/yuv.c"
    "${OMV_TOP_DIR}/ports/esp32/omv_debug.c"
    "${OMV_TOP_DIR}/ports/esp32/omv_fb_alloc.c"
    "${OMV_TOP_DIR}/ports/esp32/omv_framebuffer.c"
    "${OMV_TOP_DIR}/ports/esp32/omv_camera.c"
    "${OMV_TOP_DIR}/ports/esp32/omv_gpio.c"
    "${OMV_TOP_DIR}/ports/esp32/py_sensor.c"
)

add_library(usermod_openmv_esp32 INTERFACE)
target_sources(usermod_openmv_esp32 INTERFACE
    ${OMV_TOP_DIR}/modules/py_clock.c
    ${OMV_TOP_DIR}/modules/py_helper.c
    ${OMV_TOP_DIR}/modules/py_image.c
    ${OMV_TOP_DIR}/modules/py_imageio.c
    ${OMV_ESP32_EXTRA_SOURCES}
)
target_include_directories(usermod_openmv_esp32 INTERFACE
    ${OMV_TOP_DIR}
    ${OMV_TOP_DIR}/lib/cmsis/include
    ${OMV_TOP_DIR}/lib/imlib
    ${OMV_TOP_DIR}/common
    ${OMV_TOP_DIR}/modules
    ${OMV_TOP_DIR}/ports/esp32
    ${OMV_TOP_DIR}/boards/${MICROPY_BOARD}
)
target_compile_definitions(usermod_openmv_esp32 INTERFACE
    MICROPY_OPENMV=1
    OMV_PY_IMAGE_ESP32_MINIMAL=1
    OMV_USB_STACK_TINYUSB=1
)
target_link_libraries(usermod INTERFACE usermod_openmv_esp32)
