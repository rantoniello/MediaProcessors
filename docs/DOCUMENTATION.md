# 
Processor concept
=================

The idea behind the processor concept is to define a generic structure to wrap any multimedia processor type. By "multimedia processor", we mean any encoder, decoder, multiplexer or demultiplexer.
Namely, any "codec" or "muxer" will be  implemented as a particularization of this generic processor interface, and in consequence, they all will offer the same generic interface/API (despite each particularization may extend the common interface as desired).

XXX (figure simple square)

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

The CTRL operations, as implemented in this library, use textual information formatted in the JavaScript Object Notation (JSON).<br>
The CTR interface interoperability philosophy is based on the Representational State Transfer (REST): the processor resources are accessible as textual representations through the GET operation, and may be manipulable using the PUT operation.

To analyze the processor API, it is interesting to have a picture of how a processor fits in a typical application.<br>
In the next section we have a brief insight on [How to use a Processor](@ref How_to_use_a_Processor_the_API).

How to use a Processor: the API {#How_to_use_a_Processor_the_API}
===============================

xxx (figure threads and flow)

A typical application would use a generic processor as follows (refer to the figure XXX):<br>
-# Application prologue:
    - Firstly, we have to instantiate the processors (PROCS) module using the function 'procs_open()' which will return a module handler. With that single handler will be able to create, handle and destroy any of the processors supported types.
    - Secondly, we have to register the processor types we will support in our application. This is performed using the operations provided through the function 'procs_module_opt()'. For example, a processor type may be H.264, MP3, RTSP format, etc. Note we are registering the supported types; later we can instantiate any number of codecs/muxers of any of these registered types.
    - Open (actually instantiate type) a processor performing a POST operation on the module handler. This is requested using the 'procs_opt()' function and requires specifying the processor type and optional initial settings.<br>
    - If succeed, a unique processor identifier will be supplied to the calling application. This Id. will be used as an API parameter to handle the processor instance through the life of the application.<br> When instantiating a processor, the library will transparently initialize and launch the necessary processing threads.<br>
-# Application cyclic:
    - Launch a producer thread and a consumer thread for processing data. Use a control thread to manage processor run-time options.
    - Regarding to I/O API operations, the producer should use 'procs_send_frame()' function to put new frames of data into the processor's input FIFO buffer, and the consumer should use procs_recv_frame()' to obtain processed frames from the processor's output FIFO buffer (note that some processors, e.g. a muxer, may only implement one of these I/O functions).<br>
    For the sake of simplifying the I/O interfacing as much as possible, all the processor types use the same unique frame interface for both input and output operations: any frame will be represented using the structure 'proc_frame_ctx_s'.<br>
    - The control thread may use the function 'procs_opt()' to manage the processor options, which are basically the GET and PUT operations.<br>
    Despite the representational state can be specific for each processor, it is clearly distiguished between state-information data and the available settings. Refer to the [corresponding section](@ref How_to_use_a_Processor_the_REST) below ("How to use a Processor: the representational state") for more details.
-# Application epilogue: 
    - Close the processor using the function 'procs_opt()' to perform a DELETE operation. The library will transparently unblock I/O operations, join the processing threads and release all the related resources.<br>
    - Finally, close the processor module using the 'procs_close()' function.

How to use a Processor: the representational state {#How_to_use_a_Processor_the_REST}
==================================================

Considering that any external application can define and register a private processor type, documenting all the available processor types representational states may be an impossible task.
Despite the library defines some common data and settings for video and audio codecs, <b>the easiest -and recommendable- way to fetch any processor's REST is to actually ask the processor!</b>.

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
ret_code= procs_opt(procs_ctx, PROCS_ID_GET, proc_id, &rest_str);
@endcode

Let's suppose the answer is the following JSON string (e.g. video encoder):

@code
{
    ... any data ....
    "settings": {
         "bit_rate_output":number,
         "frame_rate_output":number,
         "width_output":number,
         "height_output":number,
         "gop_size":number,
         "conf_preset":string
    }
}
@endcode

Then, all the parameters that can be manipulated in the processor are given by "settings" object.
If we want to change the output width and height, for example, we can perform a PUT operation passing the corresponding parameters either as a query string

@code

@endcode

or as a JSON string (both are accepted)

@code

@endcode

