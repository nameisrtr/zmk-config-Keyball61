# Keyball61 right-half power guard

Right (central) half only. Add it after `keyball61_daily_power` in a right
build's shield list.

- **Wake interlock:** system-off only wakes from the keys if every matrix row
  still senses a press and every column is driven. ZMK powers off regardless:
  while a key is held the matrix polls with sensing disabled, and a press that
  lands while sleep is being entered disables it too. `sys_poweroff()` is
  wrapped so the pins are checked with interrupts locked; if the matrix could
  not wake the half, it reboots instead of powering off.
- **Sleep screen:** the right nice!view keeps its last image through
  system-off, so a sleeping right half looked frozen. A blank frame is written
  just before system-off: a blank right screen means "asleep, press a
  right-half key".
- **Split reconnect scanning:** the left half deep-sleeps after ten minutes
  without left key presses. ZMK then scans for it 30 ms out of every 60 ms
  (50% radio duty) for as long as the right stays awake. The guard keeps the
  30 ms window but opens it every 200 ms (15%).
- **Faults:** Zephyr's default fatal-error handler halts with interrupts
  locked. The guard reboots instead, so a fault costs a reconnect rather than
  a frozen half.
- **Stack margin:** the input thread, which also sends every mouse report,
  gets the 1024 bytes ZMK uses for split peripherals instead of 512.
