# Keyball61 left portrait view

This shield replaces ZMK v0.2's stock one-time balloon/mountain choice on the
left peripheral display with a persistent five-image wake cycle:

1. C2 Folded Ribbon
2. CL1 Carved Monolith
3. CL3 Skeleton Stack
4. CL5 Splitfield
5. `1-track-totem.png` (Track Totem)

Only one image is active at a time. The next index is saved using ZMK's normal
delayed settings policy, so a cold boot or key-triggered deep-sleep wake shows
the next image without a periodic slideshow timer. The battery and
split-connection status tile remains event-driven.

The physical display is portrait (`68 x 160`). Each artwork master is authored
upright at `68 x 92`, then rotated clockwise into LVGL's `92 x 68` indexed
one-bit image layout. This avoids the logical `x=92..159` status tile entirely.

Regenerate the checked-in C bitmap and preview from the repository root:

```sh
python3 tools/generate_rtr_art.py
```

Verify that all generated outputs are current without rewriting them:

```sh
python3 tools/generate_rtr_art.py --check
```
