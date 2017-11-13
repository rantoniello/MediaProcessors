# 
Application code analysis
=========================

The example source code: codecs_muxers_loopback.c

This example implements a full video coding and muxing loopback path:
- Server side:
      - generates a video raw source;
      - encodes the video source (in one of the following: H.264, MPEG2-video or MLHE);
      - multiplex the video encoded elementary stream in RTSP session;
- Client side:
       - de-multiplex video elementary stream;
       - decodes the video;
       - renders the video in a frame buffer.

The application flow in this example is analogue to the one shown in [figure 2](md_DOCUMENTATION.html#How_to_use_a_Processor_the_API), but with the following modified thread scheme:
- Server side:
      - A 'producer thread' generates raw video and sends raw YUV frames to the video encoder;
      - A 'multiplexer thread' receives encoded frames from video encoder and sends to the multiplexer (that is, to the remote client using the RTSP session);
      - The main thread is used to execute the HTTP server listening loop (until an application interruption signal -"ctrl+c"- is recieved)
- Client side:
      - A 'de-multiplexer thread' receives the encoded frames from the RTSP connection, and sends to the video decoder;
      - A 'consumer thread' finally reads the raw video frame from the decoder and renders (using the 3rd party library SDL2).

### Initializing application

Initialization consists in the same steps as the ones enumerated in the "typical application prologue" (see [the API documentation](md_DOCUMENTATION.html#How_to_use_a_Processor_the_API)):

-# Initialize (open) the processors (PROCS) module (this is done by calling function 'procs_module_open()');
-# Register the processor types we support in the application (performed using the operation 'PROCS_REGISTER_TYPE' provided through the function 'procs_module_opt()');
-# Create (open) an instance of the PROCS module (using the function 'procs_open()' which returns an instance handler);
-# Creates specific processors instances (using the operation 'PROCS_POST' with the function 'procs_opt()'; a unique processor identifier is supplied for each instance):
         - Create a video encoder instance
         - Create a video decoder instance
         - Create a RTSP multiplexor instance
         - Create a RTSP de-multiplexor instance

Nevertheless, there are some new considerations when initializing and handling the multiplexer and demultiplexer processors. We have an insight on this in the next subsections.

#### Initializing and handling the multiplexer

After instantiating the multiplexer, we must register all the elementary streams we will be serving (in this example only a video stream is served, but we may add other video or audio streams).<br>
This is done using the following API call:
@code
ret_code= procs_opt(procs_ctx, "PROCS_ID_ES_MUX_REGISTER", mux_proc_id, mime_setting, &rest_str);
@endcode
If succeed, the <b>elementary stream identifier</b> will be returned in a JSON of the form:
@code
{"elementary_stream_id":number}
@endcode
Knowing the elementary stream Id. is essential for multiplexing; is a unique number used to discriminate to which of the multiplexed streams an input frame is to be sent.<br>

This is implemented in the code of the multiplexer thread (function 'mux_thr()').
The important detail to remark is that <b>the elementary stream Id. must be specified in the input frame using the proc_frame_ctx_s::es_id field</b>.
@code
proc_frame_ctx->es_id= thr_ctx->elem_strem_id_video_server;
ret_code= procs_send_frame(thr_ctx->procs_ctx, thr_ctx->mux_proc_id, proc_frame_ctx);
@endcode
If more than one source is used (e.g. video and audio), you must use the corresponding elementary stream Id. for sending the frames of each source.

#### Initializing and handling the de-multiplexer

Regarding to the de-multiplexer initialization, the RTSP client must be provided with the listening URL in the instantiation (e.g. "rtsp_url=rtsp://127.0.0.1:8574/session").

When handling the de-mutiplexer, client application does not know at first instance how many sources are carried in a session. Thus, once the session is established and the multimedia streaming started -that is, when receiving the first frame-, the de-multiplexer API should be used to know the elementary streams carried and the identifiers assigned to each one. It is important to remark that the elementary streams identifiers used at the multiplexer are decoupled of the ones used at the de-multiplexer (in fact, in the case of the RTSP implementation, the de-multiplexer uses the service port as the Id., and the multiplexer use an incrementing counter).<br>
We ask then for the state of the demutiplexer when receiving the first frame (see de-multiplexer thread function 'dmux_thr()'):
@code
ret_code= procs_opt(thr_ctx->procs_ctx, "PROCS_ID_GET", thr_ctx->dmux_proc_id, &rest_str);
@endcode
The answer will be of the form:
@code
{
   "settings":{
      "rtsp_url":"rtsp://127.0.0.1:8574/session"
   },
   "elementary_streams":[
      {
         "sdp_mimetype":"video/MP2V",
         "port":59160,
         "elementary_stream_id":59160
      }
   ]
}
@endcode
This response should be parsed to obtain the elementary stream Id. It will be used to identify which decoder/processor to send each received frame (the stream identifier will be specified in the proc_frame_ctx_s::es_id fied of the received frame).<br>
In this example, we just have one video stream so all the frames received from the de-multiplexer will have the same stream Id.

Running the application
=======================

Just type in a shell:

> LD_LIBRARY_PATH=<...>/MediaProcessors_selfcontained/_install_dir_x86/lib <...>/MediaProcessors_selfcontained/_install_dir_x86/bin/mediaprocs_codecs_muxers_loopback

Example:
@code
MediaProcessors_selfcontained$ LD_LIBRARY_PATH=./_install_dir_x86/lib ./_install_dir_x86/bin/mediaprocs_codecs_muxers_loopback 
Created new TCP socket 4 for connection
Starting server...
live555_rtsp.cpp 2290 Got a SDP description: v=0
o=- 1508643478136254 1 IN IP4 192.168.1.37
s=n/a
i=n/a
t=0 0
a=tool:LIVE555 Streaming Media v2017.07.18
a=type:broadcast
a=control:*
a=range:npt=0-
a=x-qt-text-nam:n/a
a=x-qt-text-inf:n/a
m=video 0 RTP/AVP 96
c=IN IP4 0.0.0.0
b=AS:3000
a=rtpmap:96 mp2v/90000
a=control:track1

live555_rtsp.cpp 2358 [URL: 'rtsp://127.0.0.1:8574/session/'] Initiated the sub-session 'video/MP2V' (client port[s] 59160, 59161)
live555_rtsp.cpp 2417 [URL: 'rtsp://127.0.0.1:8574/session/'] Set up the sub-session 'video/MP2V' (client port[s] 59160, 59161)
live555_rtsp.cpp 2459 [URL: 'rtsp://127.0.0.1:8574/session/'] Started playing session...
@endcode

A window should appear rendering a colorful animation:

<img align="left" src="../img4_examples_mediaprocs_codecs_muxers_loopback.png" alt style="margin-right:90%;">
<em align="left">Figure 4: Rendering window; mediaprocs_codecs_muxers_loopback example.</em>

### Using the RESTful API

In the following lines we attach some examples on how to perform RESTful requests in run-time.<br>
For this purpose, we will use [CURL](https://curl.haxx.se/) HTTP client commands from a shell.<br>

We assume the 'mediaprocs_codecs_muxers_loopback' application is running.

To get the general representation of the running processors, type:
@code
$ curl -H "Content-Type: application/json" -X GET -d '{}' "127.0.0.1:8088/procs.json"
{
   "code":200,
   "status":"OK",
   "message":null,
   "data":{
      "procs":[
         {
            "proc_id":0,
            "proc_name":"ffmpeg_m2v_enc",
            "links":[
               {
                  "rel":"self",
                  "href":"/procs/0.json"
               }
            ]
         },
         {
            "proc_id":1,
            "proc_name":"ffmpeg_m2v_dec",
            "links":[
               {
                  "rel":"self",
                  "href":"/procs/1.json"
               }
            ]
         },
         {
            "proc_id":2,
            "proc_name":"live555_rtsp_mux",
            "links":[
               {
                  "rel":"self",
                  "href":"/procs/2.json"
               }
            ]
         },
         {
            "proc_id":3,
            "proc_name":"live555_rtsp_dmux",
            "links":[
               {
                  "rel":"self",
                  "href":"/procs/3.json"
               }
            ]
         }
      ]
   }
}
@endcode

In the response above you will find the list of the instantiated processors specifying:
- the API identifier corresponding to each processor;
- the processor name;
- the link to the processor representational state.

To get the representational state of any of the processors:
@code
$curl -H "Content-Type: application/json" -X GET -d '{}' "127.0.0.1:8088/procs/0.json"
{
   "code":200,
   "status":"OK",
   "message":null,
   "data":{
      "latency_avg_usec":35502,
      "settings":{
         "proc_name":"ffmpeg_m2v_enc",
         "bit_rate_output":307200,
         "frame_rate_output":15,
         "width_output":352,
         "height_output":288,
         "gop_size":15,
         "conf_preset":null
      }
   }
}
@endcode
@code
$curl -H "Content-Type: application/json" -X GET -d '{}' "127.0.0.1:8088/procs/1.json"
{
   "code":200,
   "status":"OK",
   "message":null,
   "data":{
      "latency_avg_usec":0,
      "settings":{
         "proc_name":"ffmpeg_m2v_dec"
      }
   }
}
@endcode
@code
$curl -H "Content-Type: application/json" -X GET -d '{}' "127.0.0.1:8088/procs/2.json"
{
   "code":200,
   "status":"OK",
   "message":null,
   "data":{
      "settings":{
         "proc_name":"live555_rtsp_mux",
         "rtsp_port":8574,
         "time_stamp_freq":9000,
         "rtsp_streaming_session_name":"session"
      },
      "elementary_streams":[
         {
            "sdp_mimetype":"video/mp2v",
            "rtp_timestamp_freq":9000,
            "elementary_stream_id":0
         }
      ]
   }
}
@endcode
@code
$curl -H "Content-Type: application/json" -X GET -d '{}' "127.0.0.1:8088/procs/3.json"
{
   "code":200,
   "status":"OK",
   "message":null,
   "data":{
      "settings":{
         "proc_name":"live555_rtsp_dmux",
         "rtsp_url":"rtsp://127.0.0.1:8574/session"
      },
      "elementary_streams":[
         {
            "sdp_mimetype":"video/MP2V",
            "port":40014,
            "elementary_stream_id":40014
         }
      ]
   }
}
@endcode

If you want to change some of the video encoder parameters, let's say the ouput width and height, do:
@code
$curl -X PUT "127.0.0.1:8088/procs/0.json?width_output=720&height_output=480"

$curl -H "Content-Type: application/json" -X GET -d '{}' "127.0.0.1:8088/procs/0.json"
{
   "code":200,
   "status":"OK",
   "message":null,
   "data":{
      "latency_avg_usec":40751,
      "settings":{
         "proc_name":"ffmpeg_m2v_enc",
         "bit_rate_output":307200,
         "frame_rate_output":15,
         "width_output":720,
         "height_output":480,
         "gop_size":15,
         "conf_preset":null
      }
   }
}
@endcode

You can even change the processor type on tun time. You have to be very careful of changing both, encoder and decoder sides.<br>
In the following code we switch from MPEG2-video to H.264 video coding:
@code
$ curl -X PUT "127.0.0.1:8088/procs/0.json?proc_name=ffmpeg_x264_enc"; curl -X PUT "127.0.0.1:8088/procs/1.json?proc_name=ffmpeg_x264_dec";

$ curl -H "Content-Type: application/json" -X GET -d '{}' "127.0.0.1:8088/procs/0.json"; curl -H "Content-Type: application/json" -X GET -d '{}' "127.0.0.1:8088/procs/1.json"
{
   "code":200,
   "status":"OK",
   "message":null,
   "data":{
      "latency_avg_usec":941490,
      "settings":{
         "proc_name":"ffmpeg_x264_enc",
         "bit_rate_output":307200,
         "frame_rate_output":15,
         "width_output":352,
         "height_output":288,
         "gop_size":15,
         "conf_preset":null,
         "flag_zerolatency":false
      }
   }
}
{
   "code":200,
   "status":"OK",
   "message":null,
   "data":{
      "latency_avg_usec":0,
      "settings":{
         "proc_name":"ffmpeg_x264_dec"
      }
   }
}
@endcode

Changing RTSP multiplexer / de-multiplexer settings is also possible. We have to take into account:
- Any change on the server or client side will break the RTSP session;
- Changes on any side, server or client, imply applying proper changes on the other side to successfully restore the RTSP session;
- Any change on the server side reset the RTSP connection. As there is a connection time-out of 60 seconds, is very feasible that you would not be able to re-use the port. In consequence, if you have to change the server settings, make sure you are also changing the server port.

Get the multiplexer and de-mutiplexer representational state:

@code
$ curl -H "Content-Type: application/json" -X GET -d '{}' "127.0.0.1:8088/procs/2.json" && 
curl -H "Content-Type: application/json" -X GET -d '{}' "127.0.0.1:8088/procs/3.json"
{
   "code":200,
   "status":"OK",
   "message":null,
   "data":{
      "settings":{
         "rtsp_port":8574,
         "time_stamp_freq":9000,
         "rtsp_streaming_session_name":"session"
      },
      "elementary_streams":[
         {
            "sdp_mimetype":"video/mp2v",
            "rtp_timestamp_freq":9000,
            "elementary_stream_id":0
         }
      ]
   }
}
{
   "code":200,
   "status":"OK",
   "message":null,
   "data":{
      "settings":{
         "rtsp_url":"rtsp://127.0.0.1:8574/session"
      },
      "elementary_streams":[
         {
            "sdp_mimetype":"video/MP2V",
            "port":40924,
            "elementary_stream_id":40924
         }
      ]
   }
}
@endcode

We will change firstly the port and the session name on the client side:

@code
$ curl -X PUT "127.0.0.1:8088/procs/3.json?rtsp_url=rtsp://127.0.0.1:8575/session2"
$ curl -H "Content-Type: application/json" -X GET -d '{}' "127.0.0.1:8088/procs/3.json"
{
   "code":200,
   "status":"OK",
   "message":null,
   "data":{
      "settings":{
         "rtsp_url":"rtsp://127.0.0.1:8575/session2"
      },
      "elementary_streams":[

      ]
   }
}
@endcode

As can be seen, the new client fail and closes the session.
Now we will change the server accordingly, set again the client (to have the effect of restarting it), and check that a new session was successfully established:

@code
$curl -X PUT "127.0.0.1:8088/procs/2.json?rtsp_port=8575&rtsp_streaming_session_name=session2"
$curl -X PUT "127.0.0.1:8088/procs/3.json?rtsp_url=rtsp://127.0.0.1:8575/session2"
$ curl -H "Content-Type: application/json" -X GET -d '{}' "127.0.0.1:8088/procs/3.json"
{
   "code":200,
   "status":"OK",
   "message":null,
   "data":{
      "settings":{
         "rtsp_url":"rtsp://127.0.0.1:8575/session2"
      },
      "elementary_streams":[
         {
            "sdp_mimetype":"video/MP2V",
            "port":32916,
            "elementary_stream_id":32916
         }
      ]
   }
}
@endcode
