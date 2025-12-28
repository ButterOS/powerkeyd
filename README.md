# powerkeyd

Android-like power button daemon for embedded Sway systems.

## Features
- Short press: toggle screen off/on (backlight only)
- Long press: show power menu via wlogout
- Replaces swayidle with systemd idle timers

## Build
meson setup build
meson compile -C build
meson install -C build

## Enable
systemctl daemon-reload
systemctl enable --now powerkeyd.service
systemctl enable --now os-lock.timer os-dim.timer os-screenoff.timer
