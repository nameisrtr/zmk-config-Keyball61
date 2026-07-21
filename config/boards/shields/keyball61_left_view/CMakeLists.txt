# SPDX-License-Identifier: MIT

if(CONFIG_SHIELD_KEYBALL61_LEFT_VIEW)
  if(CONFIG_SHIELD_NICE_VIEW)
    message(FATAL_ERROR "keyball61_left_view replaces nice_view; do not enable both shields")
  endif()

  if(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    message(FATAL_ERROR "keyball61_left_view is only supported on the peripheral left half")
  endif()

  if(CONFIG_ZMK_DISPLAY AND CONFIG_NICE_VIEW_WIDGET_STATUS)
    # Reuse the pinned ZMK v0.2 drawing helpers and provide a local peripheral
    # widget for persistent boot/wake art selection.
    zephyr_library()
    zephyr_library_include_directories(
      ${CMAKE_SOURCE_DIR}/include
      ${CMAKE_SOURCE_DIR}/boards/shields/nice_view/widgets
    )
    zephyr_library_sources(
      ${CMAKE_SOURCE_DIR}/boards/shields/nice_view/custom_status_screen.c
      ${CMAKE_SOURCE_DIR}/boards/shields/nice_view/widgets/bolt.c
      ${CMAKE_SOURCE_DIR}/boards/shields/nice_view/widgets/util.c
      widgets/peripheral_status.c
      widgets/art.c
    )
  endif()
endif()
