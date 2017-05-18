CrossSock Networking API
========================

[<img align="right" src="https://raw.github.com/CAVE-Lab/CrossSock/master/Resources/CrossSock.png" width="272" height="272"/>](https://raw.github.com/CAVE-Lab/CrossSock/master/Resources/CrossSock.png)
CrossSock is a lightweight socket library developed on top of berkley sockets (POSIX / Winsock). CrossSock has the following advantages over raw system calls:

 - Type-safe: CrossSock is safe, as sockets are encapsulated into classes.
 - Simple: CrossSock is high level and performs many of the tedious tasks involved in networked applications.
 - Header-only: CrossSock is easy to add into any existing project, just include the headers and you are all set!
 - Multi-use: CrossSock includes a low-level socket.
 - Cross-platform: Works with UNIX and Windows based machines.

Current Version
---------------

This is version: 1.0

Installing
----------

CrossSock is a header-only library, and so no installation is necessary. Simple include the header files that you need, and it is ready to go!

Note that CrossSock requires c++ 11 support, and so be sure to compile with the -std=c++11 flag.

A client and server example projects are available for Visual Studios 2015 in the Examples folder.  Please see CrossClientDemo.cpp and CrossServerDemo.cpp for general use of CrossSock.

Features
========

Connection and Connectionless Sockets
-------------------------------------

A low-level socket API is included in CrossSock.h that supports UDP and TCP sockets.

Packets and System Utility
--------------------------

CrossUtil.h includes standalone system utility, such as a timer and sleep functions, delegation, and endianness conversions can be found in.

CrossPack.h includes a high-level packet implementation. See the files for more details.

Client-server Architecture
--------------------------

A high-level client-server architecture is included in the CrossClient.h and CrossServer.h files. Please see the example projects for general use.

License
=======

Licensed under the BSD License. Please see the LICENSE file included with the CrossSock source code.