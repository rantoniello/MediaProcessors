# MediaProcessors
Generic library for multimedia processing and streaming with REST-based control 
interface.

Dependencies:
=============

MediaProcessors depends on the following third party libraries (concrete 
version tested and license is specified in parenthesis):

> cjson (1.5.8; MIT)<br>
> lame (3.99.5; LGPL)<br>
> LHE which is a FFmpeg adaptation (3.3; LGPL and GPL)<br>
> live555 (version dated July 18, 2017; LGPL)<br>
> mongoose (6.8; GPL)<br>
> nasm (2.14rc0; 2-clause BSD)<br>
> pcre (8.40 11; BSD)<br>
> sdl (2.0;  zlib license)<br>
> unittest-cpp (2.0.0, MIT)<br>
> uriparser (0.8.2.; The 3-Clause BSD)<br>
> valgrind (3.13.0, GPL)<br>
> x264 (July, 2017 , GPL)<br>
> yasm (1.3.0, BSD)<br>

GPL dependencies are linked only if configuration option --enable-gpl is enabled; 
otherwise, all the rest of the dependencies are used provided compatibility with 
the LGPL 3+ licensing.

BUILD & INSTALLATION:
=====================

OS required: Linux.<br>
Successfully tested on Ubuntu 16.04 LTS.

MediaProcessors provides four libraries:

1.- utils: Common tools used by the rest of libraries.<br>
Dependencies:
> uriparser

2.- procs: Offers a generic interface for all multimedia processors. Multimedia 
codecs and multiplexers are implemented as a particularization of the generic 
processor interface (and in consequence, all codecs and muxers offer the same 
generic interface/API).<br>
Dependencies:
> utils<br>
> uriparser<br>
> cjson<br>
> valgrind, unittest-cpp for unit-testing

3.- codecs: Wrappers/implementations of several audio and video codecs.<br>
Dependencies:
> utils<br>
> procs<br>
> uriparser<br>
> cjson<br>
> lame<br>
> x264<br>
> FFmpeg libraries and its dependencies (nasm, yasm)<br>
> mongoose, valgrind, unittest-cpp for unit-testing

4.- muxers: Wrappers and implementations of several multimedia 
multiplexing/demultiplexing formats.<br>
Dependencies:
> utils<br>
> procs<br>
> uriparser<br>
> cjson<br>
> Live555 lbraries and its dependencies<br>
> mongoose, valgrind, unittest-cpp for unit-testing

Each library can be configured and compiled as usual, providing a prefix if 
desired. That is:
> $ ./configue<br>
> $ make clean; make all

*ALTERNATIVE INSTALLATION:* (recommended)<br>
To facilitate compilation and installation, a self-contained wrapper is provided 
in the repository named 'MediaProcessors_selfcontained' 
(https://github.com/rantoniello/MediaProcessors_selfcontained).
Download or clone the repo 'MediaProcessors_selfcontained' and follow the README 
steps.

Documentation
=====================

First o all, please <b>generate the documentation</b>.
Go to the 'docs' folder

> $ cd MediaProcessors/docs/

and perform

> $ doxygen Doxyfile

Change to the 'docs/html' folder

> $ cd html

and open the file 'index.html' (documentation index) with any browser. 

Now please continue ahead with MediaProcessors [documentation](md_DOCUMENTATION.html)<br>
(To see documentation directly from GitHub follow [this link](https://rantoniello.github.io/MediaProcessors/html/md_DOCUMENTATION.html))
