SC Controller (C fork)
=============

User-mode driver and mapper for Steam Controller, DS4 and similar controllers.

-----------

## WIP Windows/BSD/Linux/android port in c

I am not actively supporting Windows, support may break in the immediate future.

This is a WIP branch of the C fork of sc-controller in which I'm removing the GUI functionality to make a simpler binary capable of simple kbm input

It should be somewhat usable with effort, but there is no GUI and is only the daemon plus keyboard right now.

-----------

#### Like what we're doing?

[![Help support the original developer, Kozec, on Liberapay](https://img.shields.io/badge/Help%20me%20become%20filthy%20rich%20on-Liberapay-yellow.svg)](https://liberapay.com/kozec) <sup>or</sup> [![donate anything with PayPal](https://img.shields.io/badge/donate_anything_with-Paypal-blue.svg)](https://www.paypal.com/cgi-bin/webscr?cmd=_donations&business=77DQD3L9K8RPU&lc=SK&item_name=kozec&item_number=scc&currency_code=EUR&bn=PP%2dDonationsBF%3abtn_donate_LG%2egif%3aNonHosted)

#### The coffee pledge

This is a project I started in early 2022 as I needed better performance for keyboard mouse inputs with my steam controller. Updates are primarily supported entirely by myself. If you get some use out of it feel free to support the development of the app or make some suggestions to steer development! 

[![ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/A0A2N1AXM)


#### Building

Navigate to directory with sources and use meson to compile:

###### on Linux (TODO: to be updated)
```
$ meson build
$ ninja -C build
$ ninja -C build scc-daemon
```

###### on Windows
```
# (you'll need mingw)
$ pacman -S --needed mingw-w64-i686-pkg-config mingw-w64-i686-meson mingw-w64-i686-gcc mingw-w64-i686-python2 mingw-w64-i686-gtk3 mingw-w64-i686-libusb mingw-w64-i686-gcc-libs mingw-w64-i686-gettext  mingw-w64-i686-gmp mingw-w64-i686-libiconv  mingw-w64-i686-libsystre mingw-w64-i686-libtre mingw-w64-i686-libwinpthread mingw-w64-i686-mpc mingw-w64-i686-mpfr mingw-w64-i686-gtk3 mingw-w64-i686-python2-gobject2

$ export PROCESSOR_ARCHITEW6432=x86
$ meson build
$ ninja -C build scc-daemon     # start without gui to check why it doesn't work

```
To run the code and detect the steam deck you must copy all the the driver files produced when running make-win32-release.exe to a new folder in the root called drivers.

```
$ ninja -C build sc-controller  # start GUI
```

This no longer works because the GUI isn't supported.

###### Flatpak (TODO: to be updated with flatpak commands)

The flatpak is defined in com.github.kozec.scc-daemon.yml and targets the latest stable freedesktop SDK



