# OpenTTD port for Switch

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

- Download (see `assets` below), and unzip openttd_switch.zip and copy the contents over to the `/switch/` folder on your sd card, so that you have a folder `/switch/openttd` with many folders and files inside.

- Use you favorite homebrew launcher to start the game. It comes with everything you need to start playing right away.

- Note: This game takes quite long to load (~15 seconds). But it is worth the wait!

- The public domain graphics and sound sets from www.openttd.org are included.

- If you want the choice to also use the original dos game graphics and sounds, copy over the files 
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
from an original TTD game to `/switch/openttd/baseset/`.

- To install the original music, copy over all files 
```
gm/gm_ttxx.gm (xx are numbers)
```

from the folder `gm` from the original windows version of the game to `/switch/openttd/gm/`.  

You then get the choice to select `original dos` graphics, sounds, and music when you go to the game settings dialog.

# Controls

 - Left analog stick = mouse pointer control 
 - D-Pad or right analog stick = scrolling the map. 
 - A/right trigger = left mouse click 
 - B/left trigger = right mouse click 
 - Y = zoom-in 
 - X = zoom-out 
 - Hold ZL = speed up analog joystick mouse 
 - Hold ZR = slow down analog joystick mouse, useful to precisely position the pointer
 - Minus = switch button mappings between two presets: ZL/ZR = slow/fast mouse and Y/X = zoom, or ZL/ZR = zoom and Y/X = slow/fast mouse
 - Double click A / double click right trigger over a text field = bring up on-screen keyboard
 - The touch screen is used for indirect mouse input, like on a laptop touchpad. Move a single finger to move the mouse pointer. Use short tap for left click. Hold a single finger while tapping a second finger for right click. Drag with two fingers to drag and drop.

# Current Limitations

 - No network support

# Building

libtimidity:
```
git clone https://github.com/rsn8887/libtimidity
cd libtimidity/src/
make -f Makefile.switch
cp libtimidity.a $DEVKITPRO/portlibs/switch/lib/
cp timidity.h $DEVKITPRO/portlibs/switch/include/
cp ../libtimidity.pc $DEVKITPRO/portlibs/lib/pkgconfig/
```

liblzo2: 
Download https://www.oberhumer.com/opensource/lzo/download/lzo-2.10.tar.gz and extract, then
```
cd lzo-2.10
./configure CFLAGS='-fPIC' CXXFLAGS='-fPIC' --host aarch64-none-elf --prefix $DEVKITPRO/portlibs/switch/ --disable-shared --enable-static
make clean
make -j12
make install
```

liblzma:
Download and extract xz-5.2.4.tar.gz from https://tukaani.org/xz/ , then
```
cd xz-5.2.4
./configure CFLAGS='-fPIC' CXXFLAGS='-fPIC' --enable-threads=no --host aarch64-none-elf --prefix $DEVKITPRO/portlibs/switch/ --disable-shared --enable-static
cd src/liblzma
make clean
make -j12
make install
```

Then configure and build OpenTTD from my switch branch:
```
git clone https://github.com/rsn8887/openttd
git checkout switch
PKG_CONFIG_PATH=$DEVKITPRO/portlibs/switch/lib/pkgconfig ./configure --os=SWITCH --host aarch64-none-elf --enable-static --prefix=/opt/devkitpro --with-sdl="pkg-config sdl2" --without-fontconfig --disable-strip --without-threads --with-libtimidity --enable-debug=0
make -j12
cd os/switch
git clean -f
cmake ./ -DCMAKE_BUILD_TYPE=Release
make openttd_switch.zip
```

openttd_switch.zip can be found in __cmake/__


# Changelog

1.03

- bundle with working AI compatibility script files (fixes AI crashing)
- bundle with pre-installed AI opponenta and support library scripts

1.02

- compile with lzma and lzo2 compression libraries to allow loading old compressed savegames/scenarios
- hold ZR / ZL for slow / fast mouse to make it easier to position the pointer with the left stick
- correctly display free disk space in load/save dialogs
- press minus to switch button mappings between two presets: ZL/ZR = slow/fast mouse, Y/X = zoom, or ZL/ZR = zoom, Y/X = slow/fast mouse

1.01

- music is now working
- improve smoothness of mouse pointer motion
- reduce analog joystick deadzone for better mouse pointer control
- update graphics and sound files to latest freeware versions from www.openttd.org
- enable cursor key map scrolling on USB keyboard
- enabled mouse wheel zoom

1.00

- initial Switch release based on latest nightly
