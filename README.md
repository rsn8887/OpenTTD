# OpenTTD port for Switch v1.01

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

The public domain graphics and sound sets from www.openttd.org are included.

If you want the choice to also use the original dos game graphics and sounds, copy over the files 
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
from the original TTD game to `/switch/openttd/baseset/`

You then get the choice to select `original dos` graphics and sounds when you go to game settings from the title screen.

# Controls

 - Left analog stick = mouse pointer control 
 - D-Pad or right analog stick = scrolling the map. 
 - A/right trigger = left mouse click 
 - B/left trigger = right mouse click 
 - Y = zoom-in 
 - X = zoom-out 
 - Double click A / double click right trigger over a text field = bring up on-screen keyboard
 - The touch screen is used for indirect mouse input, like on a laptop touchpad. Move a single finger to move the mouse pointer. Use short tap for left click. Hold a single finger while tapping a second finger for right click. Drag with two fingers to drag and drop.

# Current Limitations

 - No network support

# Building

To compile from source, first build and install libtimidity (midi music library):
```
git clone https://github.com/rsn8887/libtimidity
cd libtimidity/src/
make -f Makefile.switch
cp libtimidity.a $DEVKITPRO/portlibs/switch/lib/
cp timidity.h $DEVKITPRO/portlibs/switch/include/
cp ../libtimidity.pc $DEVKITPRO/portlibs/lib/pkgconfig/
```

Then configure and build OpenTTD from my switch branch:
```
git clone https://github.com/rsn8887/openttd
git checkout switch
PKG_CONFIG_PATH=$DEVKITPRO/portlibs/switch/lib/pkgconfig ./configure --os=SWITCH --host aarch64-none-elf --enable-static --prefix=/opt/devkitpro --with-sdl="pkg-config sdl2" --without-fontconfig --disable-strip --enable-network=0 --without-liblzo2 --without-lzma --without-threads --with-libtimidity
make -j12
cd cmake
cmake ./ -DSWITCH_BUILD=ON
make openttd_switch.zip
```

openttd_switch.zip can be found in __cmake/__


# Changelog

1.01

- enabled music
- improve smoothness of mouse pointer motion
- reduce analog joystick deadzone for better mouse pointer control
- update graphics and sound files to latest freeware versions from www.openttd.org
- enable cursor key map scrolling on USB keyboard
- enable mouse wheel zoom on USB mouse

1.00

- initial Switch release based on latest nightly

