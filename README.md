# OpenTTD port for Switch v1.0

This is my Switch port of OpenTTD, a re-implementation of Transport Tycoon Deluxe.

Port by @rsn8887, based on the Vita port by @angguss. I made some control enhancements and I also enabled sound on the Vita port, too (pull request to @angguss made).

![](https://i.postimg.cc/6QkN6fLY/openttd-1.jpg)
![](https://i.postimg.cc/xCLVXN6Y/openttd-2.jpg)
![](https://i.postimg.cc/xj5zkCZv/openttd-3.jpg)
![](https://i.postimg.cc/MTvpv6fD/2019030504002300-17-F7-D349-D6-A1508-C316-B144-FC19-A67-A7.jpg)

# Thanks

Thanks to the many developers who worked on this game.

Thanks to @angguss for his excellent Vita port, on which this Switch port is based.

Thanks to my supporters on Patreon: Andyways, CountDuckula, Greg Gibson, Jesse Harlin, Özgür Karter, Matthew Machnee, and RadicalR.

# Installation

A public domain graphics set (OpenGFX) is included. If you want the choice to also use original dos game graphics, copy over the files 
```
SAMPLE.CAT 
TRHCOM.GRF
TREND.GRF
TRTITLE.GRF
TRG1.GRF
TRGC.GRF
TRGH.GRF
TRGI.GRF
TRGT.GRF  
```
to `/switch/openttd/baseset/`

If the above files are present, a new option will appear when you go to game settings from the titlescreen. There you can select between the two graphics sets `original dos` and `openGFX`.

# Controls

 - Left analog stick = mouse pointer control 
 - D-Pad or right analog stick = scrolling the map. 
 - A/right trigger = left mouse click 
 - B/left trigger = right mouse click 
 - Y = zoom-in 
 - X = zoom-out 
 - Double click A/right trigger over a text field = bring up on-screen keyboard
 - The touch screen is used for indirect mouse input, like on a laptop touchpad. Move a single finger to move the mouse pointer. Use short tap for left click. Hold a single finger while tapping a second finger for right click. Drag with two fingers to drag and drop. 

# Current Limitations

 - No music (requires midi)
 - No network support

# Building for Switch

```
PKG_CONFIG_PATH=$DEVKITPRO/portlibs/switch/lib/pkgconfig ./configure --os=SWITCH --host aarch64-none-elf --enable-static --prefix=/opt/devkitpro --with-sdl="pkg-config sdl2" --without-fontconfig --disable-strip --enable-network=0 --without-liblzo2 --without-lzma --without-threads
make -j12
cd cmake
cmake ./ -DSWITCH_BUILD=ON
make openttd_switch.vpk
```

openttd_switch.zip can be found in __cmake/__

# Building for Vita

This same codebase can be compiled for Vita, too:

```
PKG_CONFIG_PATH=$VITASDK/arm-vita-eabi/lib/pkgconfig ./configure --os=PSVITA --host arm-vita-eabi --enable-static --prefix=/usr/local/vitasdk --with-sdl="pkg-config sdl2" --without-fontconfig --disable-strip --enable-network=0 --without-liblzo2
make -j12
cd cmake
cmake -DVITA_BUILD=ON ./
make openttd.vpk
```

openttd.vpk can be found in __cmake/__

# Changelog

1.0

- initial Switch release based on latest nightly
- enabled sound fx (requires SAMPLE.CAT from original game, and enabling `original dos` sound package in game settings)
- improved touch controls
- speedup on Vita via native texture rendering
- improved analog stick mouse controls (use right stick to pan map and left stick for pointer)