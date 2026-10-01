# SPDX-License-Identifier: MIT

if(CONFIG_SHIELD_KEYBALL61_POWER_GUARD)
  if(NOT CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    message(FATAL_ERROR "keyball61_power_guard is only supported on the right (central) half")
  endif()

  zephyr_library()
  zephyr_library_include_directories(${CMAKE_SOURCE_DIR}/include)
  zephyr_library_sources(power_guard.c)

  if(CONFIG_POWEROFF)
    # Check the matrix can wake the half before every system-off.
    zephyr_ld_options(-Wl,--wrap=sys_poweroff)
  endif()

  if(CONFIG_ZMK_SPLIT_BLE)
    # Route ZMK's split-central scan start through power_guard.c.
    zephyr_ld_options(-Wl,--wrap=bt_le_scan_start)
  endif()
endif()
