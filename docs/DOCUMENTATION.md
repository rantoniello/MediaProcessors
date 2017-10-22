# 
Processor concept
=================

The basic idea is to define a generic structure to wrap any multimedia processor type. By "multimedia processor" we mean any encoder, decoder, multiplexer or demultiplexer.
Namely, any "codec" or "muxer" will be  implemented as a particularization of this generic processor interface, and in consequence, they all will offer the same generic interface/API (despite each particularization may extend the common interface as desired).

A processor defined in this library has two basic forms of interfacing:<br>
1.- Control (CTRL) interface;<br>
2.- Input/output (I/O) interface.<br>

The latter is used to send or receive processed data, and may suppose a bit-rate as high as desired. This is important to remark, as the I/O interface should carefully take into account fast-fetching and efficient data transfers without interruptions.<br>
The former suppose insignificant bit-rate, but should be unlinked -as possible- to the I/O interface; in other words, control commands should not interfere with I/O operations performance.<br>
The processor generic implementation provides not only these characteristics, but also:<br>
- I/O operations and CTRL operations can be executed concurrently without any risks (e.g. you can delete a processor while other thread is performing I/O operations on it without crashing risk);<br>
- CTRL operations are though to change the processor state (PUT operation) asynchronously -any-time- and concurrently. This may imply changing I/O data characteristics;<br>
- CTRL operations are though to get the processor state (GET operation ) asynchronously and concurrently;<br>
- Analogously, through CTRL operations we may create (POST operation) or delete (DELETE operation) a processor asynchronously and concurrently.<br>

In view of the above, the processor API has the following fundamental operations:<br>
-# Control (CTRL) interface
    - POST<br>
    - DELETE<br>
    - GET<br>
    - PUT
-# Input/output (I/O) interface
    - send<br>
    - recieve

<img align="left" src="../img1_processor_small.png" alt style="margin-left:10%;margin-right:90%;">
<em align="left" style="margin-left:calc(10% + 100px);">Figure 1: The processor structure</em>

The CTRL operations, as implemented in this library, use textual information formatted in the JavaScript Object Notation (JSON).<br>
The CTR interface interoperability philosophy is based on the Representational State Transfer (REST): the processor resources are accessible as textual representations through the GET operation, and may be manipulable using the PUT operation.

To analyze the processor API, it is interesting to have a picture of how a processor fits in a typical application.<br>
In the next section we have a brief insight on [How to use a Processor](@ref How_to_use_a_Processor_the_API).

How to use a Processor: the API {#How_to_use_a_Processor_the_API}
===============================

A typical application would use a generic processor as follows (refer to the figure 2):<br>

<img align="left" src="../img2_processor_flow_chart_small.png" alt style="margin-left:10%;margin-right:90%;">
<em align="left" style="margin-left:calc(10% + 300px);">Figure 2: Typical processing flow</em>

- Application prologue:
    -# Initialize (open) the processors (PROCS) module to be able to use it through the life of the application (this is done by calling function 'procs_module_open()' only once at the beginning of the app.).
    -# Register the processor types you will support in your application. This is performed using the operation 'PROCS_REGISTER_TYPE' provided through the function 'procs_module_opt()'.<br>
By "processor type" we mean, for example, H.264 codec, MP3 codec, RTSP format, etc. Note that at this step we are registering the supported types; later we will be able to instantiate any number of codecs/muxers of any of the types that we registered.
    -# Create (open) an instance of the PROCS module using the function 'procs_open()' which will return an instance handler. With that single handler will be able to create, manipulate and destroy any instance of any specific processor type.
    -# Actually create a processor instance (of any of the registered types) by performing a POST operation on the module handler. This is requested using the operation 'PROCS_POST' with the function 'procs_opt()' and requires specifying the processor type and optional initial settings. <br>
If succeed, a unique processor identifier will be supplied to the calling application. This Id. will be used as an API parameter to handle the processor instance through the entire life of the application (or until the instance is deleted).<br>
When instantiating a processor, the library will transparently initialize and launch the necessary processing threads.
    -# Typically launch a producer thread and a consumer thread for processing data (I/O operations). Also, use a control thread to manage control operations (PUT, GET) in run-time.
<br>
- Application cyclic:
    -# Regarding to I/O API operations (right hand side of figure 2), the producer should use 'procs_send_frame()' function to put new frames of data into the processor's input FIFO buffer, and the consumer should use 'procs_recv_frame()' to obtain processed frames from the processor's output FIFO buffer (note that some processors, e.g. a muxer, may only implement one of these I/O functions).<br>
    For the sake of simplifying the I/O interfacing as much as possible, all the processor types (codecs or muxers) use the same unique frame interface for both input and output operations: any frame will be represented using the structure 'proc_frame_ctx_s'.<br>
    -# The control thread may use the function 'procs_opt()' to manage the processor options, which are basically the GET ('PROCS_ID_GET') and PUT ('PROCS_ID_PUT') operations.<br>
    Despite the representational state can be specific for each processor, the representation clearly separates between state-information data (which is unmodifiable) and the available settings (modifiable parameters). Refer to the [corresponding section](@ref How_to_use_a_Processor_the_REST) below ("How to use a Processor: the representational state") for more details.
<br>
- Application epilogue:
    -# Delete running processor instance using the function 'procs_opt()' to perform a 'PROCS_ID_DELETE' operation. The library will transparently unblock I/O operations, join the processor's internal processing threads and release all the related resources.<br>
It is important to remark that the <b>processor instance deletion must be performed before trying to join any of the producer or consumer thread</b>. This is because by deleting a processor we make sure we will not get blocked in an I/O operation.
    -# Join our application threads.
    -# Delete (close) the instance of the PROCS module using function 'procs_close()'. The handler will be released.
    -# Finally, close the PROCS module using the 'procs_module_close()' function.
<br>

The representational state {#How_to_use_a_Processor_the_REST}
==========================

Considering that any external application can implement and register a private processor type, doing a formal documentation of all the available processor types representational states may be an impossible task.
Despite the library defines some common data and settings for video and audio codecs, <b>the easiest -and recommendable- way to fetch any processor's REST is to actually "ask" this information to the processor</b> (request through the API).<br>
The only thing you need to know is the general form of the REST all the processors should comply, which is represented in the following JSON string:<br>
@code
{
    "settings":
    {
        ... here you will find the settings ...
    },
    ... anything outside object 'settings' is state-information data -informative, cannot be modified-
}
@endcode

The following sample code shows how to GET a processor's instance REST (see 'procs_opt()' function):

@code
char *rest_str= NULL;
...
ret_code= procs_opt(procs_ctx, "PROCS_ID_GET", proc_id, &rest_str);
@endcode

Let's suppose the answer is the following JSON string (e.g. given by a video encoder):

@code
{
    ... any data ....
    "settings": {
         "bit_rate_output":1024000,
         "frame_rate_output":25,
         "width_output":720,
         "height_output":576,
         "gop_size":25,
         "conf_preset":"ultrafast"
    }
}
@endcode

Then, all the parameters that can be manipulated in the processor are specified within the "settings" object.
If we want to change the output width and height, for example, we can perform a PUT operation passing the corresponding parameters either as a query string

@code
ret_code= procs_opt(procs_ctx, "PROCS_ID_PUT", proc_id, "width_output=352&height_output=288");
@endcode

or as a JSON string (both are accepted)

@code
ret_code= procs_opt(procs_ctx, "PROCS_ID_PUT", proc_id, 
        "{\"width_output\":352,\"height_output\":288}");
@endcode

Requesting the settings REST to the processor is effective but may be not always self-explanatory (parameters are not documented in a REST response).
To get formal documentation of each specific processor (any codec or muxer) you will have to go to the processor's code and analyze corresponding settings structure and the associated doxygen specifications.<br>
To do that, consider the following example.<br>
The video settings which are common to all video codec types ("generic" video codec settings) are defined at the structures video_settings_enc_ctx_s and video_settings_dec_ctx_s (encoding and decoding respectively) at the 'codecs' library. Similarly, common audio settings are defined at audio_settings_enc_ctx_s and audio_settings_dec_ctx_s.<br>
Any video codec is supposed to use this common settings and extend them as desired. As a concrete example, the H.264 encoder implemented at ffmpeg_x264.c (MediaProcessors's wrapper of the FFmpeg x.264 codec facility) extends the video common settings in the structure defined as ffmpeg_x264_enc_settings_ctx_s. Then, all the settings for this specific codec are documented at ffmpeg_x264_enc_settings_ctx_s and the extended common structure video_settings_enc_ctx_s.<br>
The rest of the codecs implemented in the 'codecs' library use analogue structure extensions as the above mentioned, so you can generalize this rule to see the settings parameters of any codec type implementation.

The RESTful HTTP/web-services adapter {#How_to_use_a_Processor_the_RESTful}
=====================================

REST philosophy of the API enables straightforward implementation of an HTTP web service exposing the CTRL API. Figure 3 depicts the basic scheme:

 - First of all, an HTTP server has to be integrated in the solution (later, you will see a full [example](md_EXAMPLES.html) code running a 3rd party server);
 - The HTTP server you integrate will offer to your application a kind of Common Gateway Interface (CGI). The CGI typically exposes:
     - The destination URL of the HTTP request;
     - The request method (POST, DELETE, GET or PUT);
     - A query string with parameters (if any);
     - Body content if present.
 - The RESTful adapter translates the HTTP request to a corresponding CTRL API request, and returns a response back to be used by the server.

Translation of an HTTP request to a CTRL API request is immediate, and is implemented for reference, and for your application, at procs_api_http.h / procs_api_http.c.<br>
The function 'procs_api_http_req_handler()' is in charge of translating the HTTP request and returning the corresponding response.<br>

<img align="left" src="../img3_processor_restful_adaptation_small.png" alt style="margin-left:10%;margin-right:90%;">
<em align="left" style="margin-left:calc(10% + 250px);">Figure 3: A RESTful HTTP/web-services adapter</em>

#### RESTful wrapped responses

The function 'procs_api_http_req_handler()' adds a JSON wrapper object to the response given by the CTRL API. This wrapper is a proper adaptation to the HTTP environment.<br>
Because in many web-services frameworks (e.g. JavaScript) the HTTP status response codes can not be easily reached by end-developers, a wrapped response is included in the message body with the following properties:
    - code: contains the HTTP response status code as an integer;
    - status: contains the text “success”, “fail”, or “error”; where “fail” is for HTTP status response values from 500-599, “error” is for statuses 400-499, and “success” is for everything else (e.g. 1XX, 2XX and 3XX responses).
    - message: only used for “fail” and “error” statuses to contain the error message.
    - data: Contains the response body. In the case of “error” or “fail” statuses, data may be set to 'null'.

Schematically, the RESTful adapter response has then the following form:
@code
{
    "code":number,
    "status":string,
    "message":string,
    "data": {...} // object returned by PROCS CTR API if any or null.
}
@endcode

#### Returning and requesting representation extensions

For this project RESTful specification, services use the file extension '.json' 
(e.g. all the request will have the form: 'http://server_url:port/my/url/path.json').

#### Pluralization

For this project RESTful specification, the use of pluralizations in name nodes is mandatory and generalized.<br>
Example:<br>
For referencing a specific processor with identifier '0' , we always use (note the plural 'proc<b>s</b>' at the URL path):
@code
http://server_url:port/procs/:processor_id.json
@endcode
rather than:<br>
@code
http://server_url:port/proc/:processor_id.json
@endcode

#### RESTful minimal hyper-linking practices

The RESTful API is navigable via its links to the various components of the representation.<br>
Example:<br>
Consider we are running our MediaProcessor based application with a 3rd party HTTP server with address 127.0.0.1 ('localhost') listening to port 8088.
By default, the URL base the RESTful adapter (procs_api_http.h / procs_api_http.c) use is '/procs'; thus, any remote HTTP request to our application would have an URL of the form '127.0.0.1:8088/procs[...].json'.<br>
The 'entrance point' of the API is given by the base URL ('/procs'); thus, 
if we perform from a remote HTTP client the following request:<br>
@code
GET 127.0.0.1:8088/procs.json
@endcode
We will have a response similar to the following:
@code
{
   "code":200,
   "status":"OK",
   "message":null,
   "data":{
      "procs":[
         {
            "proc_id":0,
            "type_name":"my_codec_type",
            "links":[
               {
                  "rel":"self",
                  "href":"/procs/0.json"
               }
            ]
         },
         {
            "proc_id":1,
            "type_name":"other_codec_type",
            "links":[
               {
                  "rel":"self",
                  "href":"/procs/1.json"
               }
            ]
         }, ...
      ]
   }
}
@endcode

For navigating through the rest of the resources representations we just need to follow the links; e.g. for requesting the state of processor with identifier '0':
@code
GET 127.0.0.1:8088/procs/0.json
@endcode
A sample response:
@code
{
   "code":200,
   "status":"OK",
   "message":null,
   "data":{
      "settings":{
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

Processor with Id. '0' has settings; we can manipulate them using the PUT method:
@code
PUT 127.0.0.1:8088/procs/0.json?width_output=720&height_output=480
@endcode

If we request again proessor's state:
@code
{
   "code":200,
   "status":"OK",
   "message":null,
   "data":{
      "settings":{
          ...
         "width_output":720,
         "height_output":480,
         ...
      }
   }
}
@endcode

How to use a Processor: hands-on
=================================

Please continue with the [examples] (md_EXAMPLES.html) documentation.
