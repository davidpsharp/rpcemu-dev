:warning: **Attention: this is experimental and not suited to general use. Consider it untested.** :warning:

This is an experimental development repository for **RPCEmu**, an Acorn RISC PC emulator for Windows, Mac and Linux.  It is intended for use in developing and testing patches before they are submitted to the mailing list for inclusion in the original source tree.

Forked from https://github.com/Septercius/rpcemu-dev in order to attempt to provide macOS builds going forward.

The home page for RPCEmu can be found here: http://www.marutan.net/rpcemu/.

The current version of RPCEmu is 0.9.5, the version upon which this repo is still 0.9.4.

This repo builds with Qt v6.8.0 rather than older versions of Qt5 previously used with rpcemu. I don't yet know if this is a good idea.


This repository contains the following patches:

* Version 4 of the main OS X patch.
* A patch to change the magic key combination for exiting capture mouse mode.

The sections below outline each patch in more detail.

## OS X patch - version 4 

This patch provides the following:

* Keyboard support (required due to the way that QT exposes keyboard information).
* Network support, using the new SLIRP functionality added in 0.9.2.  This enables use of email, FTP, the web and so on.
* Dynamic compilation support for later versions of OS X (including High Sierra, Mojave and Catalina).
* Configurable data folder setting, allowing the application to reside in a different folder to its settings.
* A non-Mac specific fix for an issue with locating the Ethernet driver (kindly provided by David Pitt).
* A non-Mac specific fix for processing of mouse events when the application is terminating.

## Magic key/capture mouse patch

By default, the key combination for exiting mouse capture mode in RPCEmu is Ctrl+End.  There is no dedicated "End" key on Mac laptop keyboards, so this patch changes the magic keys to Ctrl+Command.
