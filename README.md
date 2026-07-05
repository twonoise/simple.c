# simple.c
simple.c is window decorator for X11, Linux, LXQT, XFCE, &amp; more

Background
==========

> I hope that some criticism you'll read here, is not repels from use this code, and, it is not for judge what is good or bad. I am just try to make things a bit better, and it is impossible without pointing to imperfections. With huge respect to all devs, who creates all this wonderfull FOSS stuff day & night and most often for little to no thanks (so, firstly, thanks to you all!).

This research have a decades long background story of progressively annoying intentional deprecations of essential mechanisms of popular Desktop Environments, at the point of view of everyday, many hours per day use and correspond eye health care.

More or less useful DEs for complex everyday work are mostly based on two essentially different tool kits, which are GTK and Qt.

As GTK was born as FOSS code, it looks more popular. There are popular DE, such as Ubuntu and XFCE, based on it. The essential property of GTK is it's all C, not C++, so the chances for not-so-experienced user to make required for his or her health related changes to look and feel behaviour of DE, are good.

GTK was just great until it started 3rd major version. It was great for both small user programs, as well as massive GIMP or whole DE interface (panels, desktop, etc). At 3.0, important functions started to deprecate, and one example is `gtk_menu_popup()`. While looks like it's just simple few lines screen pop-up menu, this quite advanced function is way more than that; it was so good so was used in some standalone projects [^1]. Now suggested replacenents `gtk_menu_popup_at_...` are required much more code to just avoid GTK-criticals, but not work in all cases anyway, like right-click on title; so, current programs, like **Emerald** [^2] **decorator**, are still use old good oldies, being in constant fear of they are just removed some day. Or, ourself re-implement the whole `gtk_menu_popup()`, yet fix its nested deprecations, is need.

The reason why GTK was great decades ago, is same as with Ubuntu and many more others. Old software was written to be working. New one is just tries to show wow-omg effects for first 5 minutes, to find sponsors; nobody cares about bugs or memory leaks. Happily, the **Qt toolkit** is so far exception of this rule. Interestingly (but also sadly), the expensive measurement units like branded spectrum analyzers, are subjected to same trouble nowadays: these designed for salesmans now, not for real hardcore work, as before.

The final point in GTK history was abandoned support of truly bitmapped, industry hardened `.bdf`/`.pcf` fonts since 2019, due to Pango 1.44. In hard competition for wow effects, and trying to support all existing writing systems and glyphs on Earth (which is not bad at all theoretically), they make huge amount of respectable and useful work, but, the bitmap fonts were dropped finally. Sadly, I've missed that moment (can't believe it's true then), and now it's too late to find required patch to revert changes.

Please do not tell me about `.otb` fonts, as they are buggy, and most bad that they always tend to be scaled, so not differs from `.ttf` versions of bitmap fonts; why `.otb` then? (Please wait for whole separate research and explanation of that. TODO).

At other side, we have Qt. It is rock solid thing so far. The original DE comes with it, is too shiny and wow-omg, and can't be tuned to turn it all off and "give me back my 2007". But there is `LXQT`. While it is not popular at all, so can't expect that required panel applets like network, thermal, and CPU monitors, are can be unified and turned in more or less everyday useable form in observable future; but, there is **no problem with bitmap fonts**. There are `qt4ct`, `qt5ct`, `qt6ct` working, so no mess with theming. Furthermore, it all will work with bitmap Chicago95 icon theme, which is a requirement for science desktop, where resources are vital. The `Compiz` window manager is fully working with `LXQT`. This is vital, as it's Color filter inverts window colors (without color damage), which is strict requirement for eye health, as not all window components can be inverted using system settings:

> Example: Use `qt5ct` (`qt6ct`, depends on your `QT_QPA_PLATFORMTHEME`), select **`Darker` theme**, start `Dolphin`, switch to List mode (`Ctrl+3`). This was reported multiple times, but nobody cares so far. There are myriads of bugs with dark themes. These bugs are blocking errors, when we care about eyes health.

We then use _white_ themes, as they're _only_ themes without incorrectly colored elements; then we just invert it all. Note that inversion is per-window, of course, as we don't want videos or photos, or correctly written programs like **Carla**, to be inverted.

Other trouble with dark theme is with icons:

<img width="245" height="271" alt="dark-theme-icons" src="https://github.com/user-attachments/assets/9fce119d-e5b5-4898-83e3-e5a588bd9c94" />

<small> _Fig. 1. Note wrong icons and border colors on dark themed context menu._ </small>

And it can not have any **general** solution, at the point of view of dark themes. Again, only we can is _do not use **dark** theme_, but use inversion.


The only working solution for per-window inversion, and without color damage like just RGB inversion, is `Compiz`, or more precisely, **`Compiz Color Filter`**.

The problem with `LXQT` is that it, as it's whole Qt base, is C++ but not C. There is no chances to fix something, or even introduce own applets. Those who can C++, are quite expensive and not have time for FOSS mostly. (Please show me that I am wrong here). Well, let's try to use LXQT.

It can be quickly observed that so called Window Decorators for Compiz, like it's internal one, and external Emerald (btw, as well as it's CCSM control panel), are uses GTK. Which makes impossible to use bitmap fonts for window pop-up actions menu (like Minimize, Close...). In order to fix this, and to reduce amount of code, making it faster and possible for others to introduce their own health care requirements to decorator, I will try to create simple decorator based on Emerald. Currently, GTK can't be excluded from it; bu, it's turns out that GTK still have working previous mechanism of 'simple' font rendering, which **support bitmap fonts**. We will use it for title. If it disappeared some day, I'll rewrite to Qt text render. For pop-up menu, I use it already: our menu will be Qt based. As there is rarely possible to use C++ QMenu from C code, I will use Python binding (PyQt) for it.

Furthermore, I've made it working with both GTK2 and GTK3, and PyQt5 and PyQt6, and it works equally good. Embedders are welcome.

Color model will be HSL based, as it is requirement to have _High Contrast_ mode, which is a pain with RGB, but quite easy with HSL. Additionally, HSL gives uniform look and makes it possible to have all correct pixels for per-window colors. HSL not slows down the operation.

I've also added equibright (CIE) compensator for HSL to RGB conversion, to get better _perceptual_ brightness experience, while still keep integer (8-bit mostly) math. It is sometimes called 'HS _P_' color model.

> A proper equibright is impossible without either (_a_) squeeze color space, or, (_b_) reduce dynamic range (brightness) of display device for an order (pure yellow to pure blue brightness ratio), so quite expensive display need, **and**, full desktop (not just one window) support. (Or, somewhere between _a_ & _b_). We use the former one here.

It's all available in one C file, with minimal possible deps, and file sizes less than 64 kb both source and binary. I hope the code is user friendly enough: please introduce your own tricks, code is _intended_ for that.

Compile
=======

* Install `Terminus` font, or other truly bitmap `.bdf`/`.pcf` fonts. Note that Terminus can be named `Terminus` on one system, and `xos4 Terminus` on another, so **correct the font name at our C code first**. Use `fc-list | grep erminus` to check. As Terminus package brings `.otb` versions also, be sure to delete these.

* Use both `qt5ct` and `qt6ct` to select `(xos4) Terminus`:
  
> Btw, note that this exactly font **does not exist** at GTK's programs font selection menus.
  
    QT_QPA_PLATFORMTHEME=qt5ct qt5ct

    QT_QPA_PLATFORMTHEME=qt6ct qt6ct
   
Then check system-wide effects on Qt software like LXQt itself. Note that one should have `QT_QPA_PLATFORMTHEME=qt_ct` at `/etc/environment`, so select your preferable QtX here for system-wide. 


* Install Chicago95 theme; we need icons only from it. Then correct icons path at C code.

* Replace `PyQt6` to `PyQt5` if need. Install the toolkit:

    `pacman -S python-pyqt_`

* Install other requirements. For `wnck`, it differs for GTK2 and GTK3:

    `yay -S libwnck`

or

    pacman -S libwnck3

* Compile:

_

    gcc -Wall -Wno-unused-result -lX11 -I/usr/include/compiz/ -ldecoration -I/usr/include/libwnck-1.0/ -lwnck-1 -I/usr/include/python3.14 -lpython3.14 `pkg-config --cflags --libs gtk+-2.0` simple.c -o /usr/local/bin/simple

or

    gcc -DGTK3 -Wall -Wno-unused-result -lX11 -I/usr/include/compiz/ -ldecoration -I/usr/include/libwnck-3.0/ -lwnck-3 -I/usr/include/python3.14 -lpython3.14 `pkg-config --cflags --libs gtk+-3.0` simple.c -o /usr/local/bin/simple

or

    clang ...

Note that, if we expand `pkg-config...`, we will see the Pango use; but it is not required and not used, and most probably, does not increase binary size.

Usage
=====

Note that, while I test it with LXQT and XFCE, it may work on other systems where `Compiz` and `ccsm` work.

Install compiz and ccsm:

    yay -S compiz

Then try

    ./simple --replace

And if it works, start `ccsm`, find "Window Decoration", "Command", use "exec /usr/local/bin/simple". Note that Compiz then can be used by LXDE directly: Main Menu, LXQt settings, Session settings, X11 settings, Window manager: `/usr/bin/compiz` .

After system update, if `ccsm` says like `ImportError: libprotobuf.so.34.1.0: cannot open shared object file: No such file or directory`, it's need to be rebuilt manually:

    yay -S --rebuild compiz

What is tested, or not
======================

* Compiz versions both 0.8.18 and 0.9.14.2 (both are current[^3]) are tested.

* Multiple workspaces/desktops/displays per one PC are not tested and testing is not planned, sorry (which does not meant that it will do not work).

* However, virtual desktop **size** (like using command below) is tested.

    `xrandr --output HDMI1 --fb 2304x1728 --panning 2304x1728 --scale 2x2`

* None of Compiz's effect, except Color filter, Negate, and Opacity, are tested (but again, should work fine).

Q & A
=====

_Where are title buttons?_

Ask me if you need it, I'll add. Note that icon is already a button, as expected.

Bugs
====

There are many, like massive memory leaks from Python libraries, and other places, please see C code. Probably, most are one leak per session, so it is not really a leak at all, as it is just memory used for program to work.

There is bug with popup menu, when we can't connect input signal to Python code when we are change active window, so current popup, if any, should gone. Good news are it's rare condition, as popups are gone itself most time by normal computer use.

To fix distorted (stretched) titlebar for `xterm`, check our source code for workaround (by word _xterm_). This occurs not on every system, and may depend on if xterm's own 1px border displayed or not. It looks like `wnck` (both `-1` & `-3`) or `xterm` issue(s). 


LICENSE
=======

This manual for `simple` decorator, as well as its inlined pictures, are licensed under Creative Commons Attribution 4.0. You are welcome to contribute to the manual in order to improve it so long as your contributions are made available under this same license.

`simple` decorator is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

References
==========

[^1] https://stackoverflow.com/questions/57911772/gtk3-gtk-menu-popup-at-pointer-without-trigger-event

[^2] https://github.com/compiz-reloaded/emerald

[^3] https://wiki.archlinux.org/title/Compiz

