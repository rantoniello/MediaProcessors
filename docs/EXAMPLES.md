# 
Application examples
====================

## How to run an example

Application examples source codes are available at

> <..>/MediaProcessors_selfcontained/MediaProcessors/examples/

Each example is compiled automatically and the corresponding binary is installed at

> <...>/MediaProcessors_selfcontained/_install_dir_x86/bin

To run, let's say, the 'codecs_muxers_loopback' example, just type:

> LD_LIBRARY_PATH=<...>/MediaProcessors_selfcontained/_install_dir_x86/lib <...>/MediaProcessors_selfcontained/_install_dir_x86/bin/mediaprocs_codecs_muxers_loopback


## codecs_muxers_loopback example

This example implements a full video coding and muxing loopback path:
- Server side:
      - generates a video raw source;
      - encodes the video source (in one of the following: H.264, MPEG2-video or MLHE);
      - multiplex the video encoded elementary stream in RTSP session;
- Client side:
       - de-multiplex video elementary stream;
       - decodes the video;
       - renders the video in a frame buffer.

Please refer to the [codecs_muxers_loopback example documentation]
(md_CODECS_MUXERS_LOOPBACK_EXAMPLE.html).
