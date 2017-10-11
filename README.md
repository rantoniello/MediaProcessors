# MediaProcessors
Generic library for multimedia processing and streaming with REST-based control 
interface.

Dependencies:
=============

MediaProcessors depends on the following third party libraries (concrete 
version tested and license is specified in parenthesis):

>cjson (1.5.8; MIT)\
>lame (3.99.5; LGPL)\
>LHE which is a FFmpeg adaptation (3.3; LGPL and GPL)\
>live555 (version dated July 18, 2017; LGPL)\
>mongoose (6.8; GPL)\
>nasm (2.14rc0; 2-clause BSD)\
>sdl (2.0;  zlib license)\
>unittest-cpp (2.0.0, MIT)\
>uriparser (0.8.2.; The 3-Clause BSD)\
>valgrind (3.13.0, GPL)\
>x264 (July, 2017 , GPL)\
>yasm (1.3.0, BSD)\

GPL dependencies are linked only if configuration option --enable-gpl is enabled; 
otherwise, all the rest of the dependencies are used provided compatibility with 
the LGPL 3+ licensing.

BUILD & INSTALLATION:
=====================

OS required: Linux.\
Successfully tested on Ubuntu 16.04 LTS.

MediaProcessors provides four libraries:

1.- utils: Common tools used by the rest of libraries.\
Dependencies:
>uriparser

2.- procs: Offers a generic interface for all multimedia processors. Multimedia 
codecs and multiplexers are implemented as a particularization of the generic 
processor interface (and in consequence, all codecs and muxers offer the same 
generic interface/API).\
Dependencies:
>utils\
>uriparser\
>cjson\
>valgrind, unittest-cpp for unit-testing

3.- codecs: Wrappers/implementations of several audio and video codecs.\
Dependencies:
>utils\
>procs\
>uriparser\
>cjson\
>lame\
>x264\
>FFmpeg libraries and its dependencies (nasm, yasm)\
>mongoose, valgrind, unittest-cpp for unit-testing

4.- muxers: Wrappers and implementations of several multimedia 
multiplexing/demultiplexing formats.\
Dependencies:
>utils\
>procs\
>uriparser\
>cjson\
>Live555 lbraries and its dependencies\
>mongoose, valgrind, unittest-cpp for unit-testing

Each library can be configured and compiled as usual, providing a prefix if 
desired. That is:
>$ ./configue\
>$ make clean; make all

*ALTERNATIVE INSTALLATION* (recommended)\
To facilitate compilation and installation, a self-contained wrapper is provided 
in the repository named 'MediaProcessors_selfcontained' 
(https://github.com/rantoniello/MediaProcessors_selfcontained).
Download or clone the repo 'MediaProcessors_selfcontained' and follow the README 
steps.
