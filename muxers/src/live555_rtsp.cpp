/*
 * Copyright (c) 2017 Rafael Antoniello
 *
 * This file is part of MediaProcessors.
 *
 * MediaProcessors is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * MediaProcessors is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with MediaProcessors. If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file live555_rtsp.cpp
 * @author Rafael Antoniello
 */

#include <mutex>

extern "C" {
#include "live555_rtsp.h"

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>

#include <libcjson/cJSON.h>
#include <libmediaprocsutils/uri_parser.h>
#include <libmediaprocsutils/log.h>
#include <libmediaprocsutils/stat_codes.h>
#include <libmediaprocsutils/check_utils.h>
#include <libmediaprocsutils/schedule.h>
#include <libmediaprocsutils/fair_lock.h>
#include <libmediaprocsutils/fifo.h>
#include <libmediaprocs/proc_if.h>
#include <libmediaprocs/procs.h>
#include <libmediaprocs/proc.h>
#include "muxers_settings.h"
#include "proc_muxer.h"
}

#include <liveMedia/liveMedia.hh>
#include <BasicUsageEnvironment/BasicUsageEnvironment.hh>
#ifndef _MULTI_FRAMED_RTP_SINK_HH
#include "MultiFramedRTPSink.hh"
#endif

using namespace std;

/* **** Definitions **** */

#define SERVER_TOUT 10

#define FRAMED_SOURCE_FIFO_SLOTS 16

#define SINK_BUFFER_SIZE 200000

//#define ENABLE_DEBUG_LOGS
#ifdef ENABLE_DEBUG_LOGS
	#define LOGD_CTX_INIT(CTX) LOG_CTX_INIT(CTX)
	#define LOGD(FORMAT, ...) LOGV(FORMAT, ##__VA_ARGS__)
#else
	#define LOGD_CTX_INIT(CTX)
	#define LOGD(...)
#endif

/**
 * Returns non-zero if given 'tag' string contains 'needle' sub-string.
 */
#define TAG_HAS(NEEDLE) (strstr(tag, NEEDLE)!= NULL)

/**
 * Returns non-zero if 'tag' string is equal to given TAG string.
 */
#define TAG_IS(TAG) (strncmp(tag, TAG, strlen(TAG))== 0)

/**
 * Live555's RTSP multiplexer settings context structure.
 */
typedef struct live555_rtsp_mux_settings_ctx_s {
	/**
	 * Generic multiplexer settings.
	 * *MUST* be the first field in order to be able to cast to
	 * mux_settings_ctx_s.
	 */
	struct muxers_settings_mux_ctx_s muxers_settings_mux_ctx;
} live555_rtsp_mux_settings_ctx_t;

/**
 * Live555's RTSP multiplexer context structure.
 */
typedef struct live555_rtsp_mux_ctx_s {
	/**
	 * Generic MUXER context structure.
	 * *MUST* be the first field in order to be able to cast to both
	 * proc_muxer_mux_ctx_t or proc_ctx_t.
	 */
	struct proc_muxer_mux_ctx_s proc_muxer_mux_ctx;
	/**
	 * Live555's RTSP multiplexer settings.
	 * This structure extends (thus can be casted to) muxers_settings_mux_ctx_t.
	 */
	volatile struct live555_rtsp_mux_settings_ctx_s
	live555_rtsp_mux_settings_ctx;
	/**
	 * Live555's TaskScheduler Class.
	 * Used for scheduling MUXER and other processing when corresponding
	 * events are signaled (e.g. multiplexing frame of data when a new frame
	 * is available at the input).
	 */
	TaskScheduler *taskScheduler;
	/**
	 * Live555's UsageEnvironment Class.
	 */
	UsageEnvironment *usageEnvironment;
	/**
	 * Live555's RTSPServer Class.
	 */
	RTSPServer *rtspServer;
	/**
	 * Live555's scheduler thread.
	 */
	pthread_t taskScheduler_thread;
	/**
	 * Live555's media sessions server.
	 */
	ServerMediaSession *serverMediaSession;
	/**
	 * Reserved for future use: other parameters here ...
	 */
} live555_rtsp_mux_ctx_t;

/**
 * Live555's RTSP elementary stream (ES) multiplexer settings context structure.
 */
typedef struct live555_rtsp_es_mux_settings_ctx_s {
	/**
	 * MIME type for this ES-MUXER.
	 * (e.g. video/H264, etc.).
	 */
	char *sdp_mimetype;
	/**
	 * Time-base for this ES-MUXER.
	 * (e.g. 9000)
	 */
	unsigned int rtp_timestamp_freq;
} live555_rtsp_es_mux_settings_ctx_t;

/**
 * Live555's RTSP elementary stream (ES) multiplexer context structure.
 */
class SimpleMediaSubsession; // Forward declaration
typedef struct live555_rtsp_es_mux_ctx_s {
	/**
	 * Generic PROC context structure.
	 * *MUST* be the first field in order to be able to cast to proc_ctx_t.
	 */
	struct proc_ctx_s proc_ctx;
	/**
	 * Live555's RTSP ES-multiplexer settings.
	 */
	volatile struct live555_rtsp_es_mux_settings_ctx_s
	live555_rtsp_es_mux_settings_ctx;
	/**
	 * External LOG module context structure instance.
	 */
	log_ctx_t *log_ctx;
	/**
	 * Live555's SimpleMediaSubsession Class.
	 */
	SimpleMediaSubsession *simpleMediaSubsession;
	/**
	 * Externally defined Live555's TaskScheduler Class.
	 * Used for scheduling MUXER and other processing when corresponding
	 * events are signaled (e.g. multiplexing frame of data when a new frame
	 * is available at the input).
	 */
	TaskScheduler *taskScheduler;
	/**
	 * Reserved for future use: other parameters here ...
	 */
} live555_rtsp_es_mux_ctx_t;

/**
 * Live555's RTSP de-multiplexer settings context structure.
 */
typedef struct live555_rtsp_dmux_settings_ctx_s {
	/**
	 * Generic de-multiplexer settings.
	 * *MUST* be the first field in order to be able to cast to
	 * mux_settings_ctx_s.
	 */
	struct muxers_settings_dmux_ctx_s muxers_settings_dmux_ctx;
} live555_rtsp_dmux_settings_ctx_t;

/**
 * Live555's RTSP de-multiplexer context structure.
 */
class SimpleRTSPClient; // Forward declaration
typedef struct live555_rtsp_dmux_ctx_s {
	/**
	 * Generic processor context structure.
	 * *MUST* be the first field in order to be able to cast to proc_ctx_t.
	 */
	struct proc_ctx_s proc_ctx;
	/**
	 * Live555's RTSP de-multiplexer settings.
	 * This structure extends (thus can be casted to)
	 * muxers_settings_dmux_ctx_t.
	 */
	volatile struct live555_rtsp_dmux_settings_ctx_s
	live555_rtsp_dmux_settings_ctx;
	/**
	 * Live555's TaskScheduler Class.
	 * Used for scheduling MUXER and other processing when corresponding
	 * events are signaled (e.g. de-multiplexing frame of data when a new frame
	 * is available at the input).
	 */
	TaskScheduler *taskScheduler;
	/**
	 * Live555's UsageEnvironment Class.
	 */
	UsageEnvironment *usageEnvironment;
	/**
	 * Live555's RTSPClient simple extension.
	 */
	SimpleRTSPClient *simpleRTSPClient;
} live555_rtsp_dmux_ctx_t;

/* **** Prototypes **** */

/* **** Multiplexer **** */

static proc_ctx_t* live555_rtsp_mux_open(const proc_if_t *proc_if,
		const char *settings_str, log_ctx_t *log_ctx, va_list arg);
static int live555_rtsp_mux_init_given_settings(
		live555_rtsp_mux_ctx_t *live555_rtsp_mux_ctx,
		const muxers_settings_mux_ctx_t *muxers_settings_mux_ctx,
		log_ctx_t *log_ctx);
static void live555_rtsp_mux_close(proc_ctx_t **ref_proc_ctx);
static void live555_rtsp_mux_deinit_except_settings(
		live555_rtsp_mux_ctx_t *live555_rtsp_mux_ctx, log_ctx_t *log_ctx);
static int live555_rtsp_mux_process_frame(proc_ctx_t *proc_ctx,
		fifo_ctx_t *iput_fifo_ctx, fifo_ctx_t *oput_fifo_ctx);
static int live555_rtsp_mux_rest_put(proc_ctx_t *proc_ctx, const char *str);
static int live555_rtsp_mux_opt(proc_ctx_t *proc_ctx, const char *tag,
		va_list arg);
static int live555_rtsp_mux_rest_get(proc_ctx_t *proc_ctx,
		const proc_if_rest_fmt_t rest_fmt, void **ref_reponse);
static int live555_rtsp_mux_rest_get_es_array(procs_ctx_t *procs_ctx_es_muxers,
		cJSON **ref_cjson_es_array, log_ctx_t *log_ctx);

static int live555_rtsp_mux_settings_ctx_init(
		volatile live555_rtsp_mux_settings_ctx_t *live555_rtsp_mux_settings_ctx,
		log_ctx_t *log_ctx);
static void live555_rtsp_mux_settings_ctx_deinit(
		volatile live555_rtsp_mux_settings_ctx_t *live555_rtsp_mux_settings_ctx,
		log_ctx_t *log_ctx);
static void* taskScheduler_thr(void *t);

static proc_ctx_t* live555_rtsp_es_mux_open(const proc_if_t *proc_if,
		const char *settings_str, log_ctx_t *log_ctx, va_list arg);
static void live555_rtsp_es_mux_close(proc_ctx_t **ref_proc_ctx);
static int live555_rtsp_es_mux_process_frame(proc_ctx_t *proc_ctx,
		fifo_ctx_t *iput_fifo_ctx, fifo_ctx_t *oput_fifo_ctx);
static int live555_rtsp_es_mux_rest_put(proc_ctx_t *proc_ctx, const char *str);
static int live555_rtsp_es_mux_rest_get(proc_ctx_t *proc_ctx,
		const proc_if_rest_fmt_t rest_fmt, void **ref_reponse);

static int live555_rtsp_es_mux_settings_ctx_init(
		volatile live555_rtsp_es_mux_settings_ctx_t *
		live555_rtsp_es_mux_settings_ctx, log_ctx_t *log_ctx);
static void live555_rtsp_es_mux_settings_ctx_deinit(
		volatile live555_rtsp_es_mux_settings_ctx_t *
		live555_rtsp_es_mux_settings_ctx, log_ctx_t *log_ctx);

/**
 * So-called "framed-sink" class prototype.
 */
class SimpleRTPSink2: public MultiFramedRTPSink {
public:
	static SimpleRTPSink2* createNew(UsageEnvironment& env, Groupsock* RTPgs,
			unsigned char rtpPayloadFormat, unsigned rtpTimestampFrequency,
			char const* sdpMediaTypeString, char const* rtpPayloadFormatName,
			unsigned numChannels= 1,
			Boolean allowMultipleFramesPerPacket= True,
			Boolean doNormalMBitRule= True);

protected:
	SimpleRTPSink2(UsageEnvironment& env, Groupsock* RTPgs,
			unsigned char rtpPayloadFormat, unsigned rtpTimestampFrequency,
			char const* sdpMediaTypeString, char const* rtpPayloadFormatName,
			unsigned numChannels, Boolean allowMultipleFramesPerPacket,
			Boolean doNormalMBitRule);
	virtual ~SimpleRTPSink2();

protected: // redefined virtual functions
	virtual void doSpecialFrameHandling(unsigned fragmentationOffset,
			unsigned char* frameStart,
			unsigned numBytesInFrame,
			struct timeval framePresentationTime,
			unsigned numRemainingBytes);
	virtual Boolean frameCanAppearAfterPacketStart(
			unsigned char const* frameStart, unsigned numBytesInFrame) const;
	virtual char const* sdpMediaType() const;

private:
	char const* fSDPMediaTypeString;
	Boolean fAllowMultipleFramesPerPacket;
	Boolean fSetMBitOnLastFrames, fSetMBitOnNextPacket;
};

/**
 * So-called "framed-source" class prototype.
 */
class SimpleFramedSource: public FramedSource
{
public:
	static SimpleFramedSource* createNew(UsageEnvironment& env,
			log_ctx_t *log_ctx);
	/**
	 * This function is called when new frame data is available from the
	 * device.
	 */
	void deliverFrame();
	/**
	 * Input frames FIFO buffer.
	 */
	fifo_ctx_t *m_fifo_ctx;
	/**
	 * Unambiguous frame consuming method event trigger identifier.
	 */
	volatile EventTriggerId m_eventTriggerId;

protected:
	SimpleFramedSource(UsageEnvironment&, log_ctx_t*);
	~SimpleFramedSource();

private:
	/**
	 * This function is called (by our 'downstream' object) when it asks for
	 * new data.
	 */
	void doGetNextFrame();
	/**
	 * Refer to SimpleFramedSource::deliverFrame() method.
	 */
	static void deliverFrame0(void* clientData);
	/**
	 * Externally provided LOG module context structure instance.
	 */
	log_ctx_t *m_log_ctx;
};

/**
 * So-called "media sub-session" class prototype.
 */
class SimpleMediaSubsession: public OnDemandServerMediaSubsession
{
public:
	static SimpleMediaSubsession *createNew(UsageEnvironment &env,
			const char *sdp_mimetype, portNumBits initialPortNum= 6970,
			Boolean multiplexRTCPWithRTP= False);

	void deliverFrame(proc_frame_ctx_t **);

protected:
	SimpleMediaSubsession(UsageEnvironment &env, const char *sdp_mimetype,
			portNumBits initialPortNum=6970,
			Boolean multiplexRTCPWithRTP=False);

	virtual FramedSource* createNewStreamSource(unsigned clientSessionId,
			unsigned& estBitrate);
	virtual void deleteStream(unsigned clientSessionId, void*& streamToken);
	virtual void closeStreamSource(FramedSource* inputSource);
	virtual RTPSink* createNewRTPSink(Groupsock* rtpGroupsock,
			unsigned char rtpPayloadTypeIfDynamic, FramedSource* inputSource);
private:
	std::mutex m_simpleFramedSource_mutex;
	SimpleFramedSource *volatile m_simpleFramedSource;
	const char *m_sdp_mimetype;
	/**
	 * Externally provided LOG module context structure instance.
	 */
	log_ctx_t *m_log_ctx;
};

/* **** De-multiplexer **** */

static proc_ctx_t* live555_rtsp_dmux_open(const proc_if_t *proc_if,
		const char *settings_str, log_ctx_t *log_ctx, va_list arg);
static int live555_rtsp_dmux_init_given_settings(
		live555_rtsp_dmux_ctx_t *live555_rtsp_dmux_ctx,
		const muxers_settings_dmux_ctx_t *muxers_settings_dmux_ctx,
		log_ctx_t *log_ctx);
static void live555_rtsp_dmux_close(proc_ctx_t **ref_proc_ctx);
static void live555_rtsp_dmux_deinit_except_settings(
		live555_rtsp_dmux_ctx_t *live555_rtsp_dmux_ctx, log_ctx_t *log_ctx);
static int live555_rtsp_dmux_rest_get(proc_ctx_t *proc_ctx,
		const proc_if_rest_fmt_t rest_fmt, void **ref_reponse);
static int live555_rtsp_dmux_process_frame(proc_ctx_t *proc_ctx,
		fifo_ctx_t* iput_fifo_ctx, fifo_ctx_t* oput_fifo_ctx);
static int live555_rtsp_dmux_rest_put(proc_ctx_t *proc_ctx, const char *str);

static int live555_rtsp_dmux_settings_ctx_init(
		volatile live555_rtsp_dmux_settings_ctx_t *
		live555_rtsp_dmux_settings_ctx, log_ctx_t *log_ctx);
static void live555_rtsp_dmux_settings_ctx_deinit(
		volatile live555_rtsp_dmux_settings_ctx_t *
		live555_rtsp_dmux_settings_ctx, log_ctx_t *log_ctx);

static void continueAfterDESCRIBE(RTSPClient *rtspClient, int resultCode,
		char *resultString);
static void continueAfterSETUP(RTSPClient *rtspClient, int resultCode,
		char *resultString);
static void continueAfterPLAY(RTSPClient *rtspClient, int resultCode,
		char *resultString);
static void subsessionAfterPlaying(void * clientData);
static void subsessionByeHandler(void *clientData);
static void setupNextSubsession(RTSPClient* rtspClient);
static void shutdownStream(RTSPClient* rtspClient, int exitCode= 1);

/**
 * This class wraps MediaSubsession with the objective of implementing our
 * particular version of the method MediaSubsession::createSourceObjects.
 */
class SimpleClientMediaSubsession: public MediaSubsession
{
public:
	SimpleClientMediaSubsession(MediaSession& parent);
protected:
	virtual Boolean createSourceObjects(int useSpecialRTPoffset);
};

/**
 * This class wraps MediaSession with the objective of implementing our
 * particular version of the method MediaSession::createNewMediaSubsession.
 * The idea is to use instead of the Live555's MediaSubsession class our
 * wrapper class SimpleClientMediaSubsession.
 */
class SimpleClientSession: public MediaSession
{
public:
	static SimpleClientSession* createNew(UsageEnvironment& env,
			char const* sdpDescription);
protected:
	SimpleClientSession(UsageEnvironment& env);
	virtual MediaSubsession* createNewMediaSubsession();
};

/**
 * This class is used to hold per-stream RTSP client state that we maintain
 * throughout each stream's lifetime.
 */
class StreamClientState {
public:
  StreamClientState();
  virtual ~StreamClientState();

public:
  MediaSubsessionIterator* iter;
  SimpleClientSession* session;
  MediaSubsession* subsession;
  TaskToken streamTimerTask;
  double duration;
};

/**
 * This class inherits RTSPClient with the objective of adding a
 * StreamClientState as a member for each stream, as other private variables.
 */
class SimpleRTSPClient: public RTSPClient {
public:
	static SimpleRTSPClient* createNew(UsageEnvironment& env,
			char const* rtspURL, volatile int *ref_flag_exit,
			fifo_ctx_t *fifo_ctx, int verbosityLevel= 0,
			char const* applicationName= NULL,
			portNumBits tunnelOverHTTPPortNum= 0, log_ctx_t *log_ctx= NULL);

	StreamClientState streamClientState;
	volatile int *m_ref_flag_exit;
	log_ctx_t *m_log_ctx;
	fifo_ctx_t *m_fifo_ctx;
protected:
	SimpleRTSPClient(UsageEnvironment& env, char const* rtspURL,
			volatile int *ref_flag_exit, fifo_ctx_t *fifo_ctx,
			int verbosityLevel, char const* applicationName,
			portNumBits tunnelOverHTTPPortNum, log_ctx_t *log_ctx= NULL);
	virtual ~SimpleRTSPClient();
};

/**
 * Define a data sink (a subclass of "MediaSink") to receive the data for
 * each sub-session (i.e., each audio or video 'sub-stream').
 */
class DummySink: public MediaSink {
public:
	/**
	 * @param env
	 * @param subsession Identifies the kind of data that's being received
	 * @param streamId Identifies the stream itself (optional)
	 */
	static DummySink* createNew(UsageEnvironment& env,
			MediaSubsession& subsession, fifo_ctx_t *fifo_ctx,
			char const* streamId= NULL, log_ctx_t *log_ctx= NULL);

private:
	DummySink(UsageEnvironment& env, MediaSubsession& subsession,
			fifo_ctx_t *fifo_ctx, char const* streamId, log_ctx_t *log_ctx);
	virtual ~DummySink();

	static void afterGettingFrame(void* clientData, unsigned frameSize,
			unsigned numTruncatedBytes,
			struct timeval presentationTime,
			unsigned durationInMicroseconds);
	void afterGettingFrame(unsigned frameSize, unsigned numTruncatedBytes,
			struct timeval presentationTime, unsigned durationInMicroseconds);
	/* redefined virtual functions */
	virtual Boolean continuePlaying();

private:
	u_int8_t* fReceiveBuffer;
	MediaSubsession& fSubsession;
	char* fStreamId;
	log_ctx_t *m_log_ctx;
	fifo_ctx_t *m_fifo_ctx;
	proc_frame_ctx_t *m_proc_frame_ctx;
	std::mutex m_dummySink_io_mutex;
};

/* **** General **** */

void live555_rtsp_reset_on_new_settings(proc_ctx_t *proc_ctx,
		int flag_is_muxer, log_ctx_t *log_ctx);
void live555_rtsp_reset_on_new_settings_es_mux(proc_ctx_t *proc_ctx,
		log_ctx_t *log_ctx);
void live555_rtsp_reset_on_new_settings_es_dmux(proc_ctx_t *proc_ctx,
		log_ctx_t *log_ctx);

/* **** Implementations **** */

extern "C" {
const proc_if_t proc_if_live555_rtsp_mux=
{
	"live555_rtsp_mux", "multiplexer", "application/octet-stream",
	(uint64_t)PROC_FEATURE_WR,
	live555_rtsp_mux_open,
	live555_rtsp_mux_close,
	live555_rtsp_mux_rest_put,
	live555_rtsp_mux_rest_get,
	live555_rtsp_mux_process_frame,
	live555_rtsp_mux_opt,
	NULL, // input proc_frame_ctx to "private-frame-format"
	NULL, // "private-frame-format" release
	NULL, // "private-frame-format" to proc_frame_ctx
};

static const proc_if_t proc_if_live555_rtsp_es_mux=
{
	"live555_rtsp_es_mux", "multiplexer", "application/octet-stream",
	(uint64_t)PROC_FEATURE_WR,
	live555_rtsp_es_mux_open,
	live555_rtsp_es_mux_close,
	NULL, //live555_rtsp_es_mux_rest_put // used internally only (not in API)
	live555_rtsp_es_mux_rest_get,
	live555_rtsp_es_mux_process_frame,
	NULL, //live555_rtsp_es_mux_opt
	NULL,
	NULL,
	NULL,
};

const proc_if_t proc_if_live555_rtsp_dmux=
{
	"live555_rtsp_dmux", "demultiplexer", "application/octet-stream",
	(uint64_t)PROC_FEATURE_RD,
	live555_rtsp_dmux_open,
	live555_rtsp_dmux_close,
	live555_rtsp_dmux_rest_put,
	live555_rtsp_dmux_rest_get,
	live555_rtsp_dmux_process_frame,
	NULL, //live555_rtsp_dmux_opt,
	NULL, // input proc_frame_ctx to "private-frame-format"
	NULL, // "private-frame-format" release
	NULL, // "private-frame-format" to proc_frame_ctx
};
} //extern "C"

/**
 * Implements the proc_if_s::open callback.
 * See .proc_if.h for further details.
 */
static proc_ctx_t* live555_rtsp_mux_open(const proc_if_t *proc_if,
		const char *settings_str, log_ctx_t *log_ctx, va_list arg)
{
	int ret_code, end_code= STAT_ERROR;
	live555_rtsp_mux_ctx_t *live555_rtsp_mux_ctx= NULL;
	volatile live555_rtsp_mux_settings_ctx_t *live555_rtsp_mux_settings_ctx=
			NULL; // Do not release
	volatile muxers_settings_mux_ctx_t *muxers_settings_mux_ctx=
			NULL; // Do not release
	LOG_CTX_INIT(log_ctx);

	/* Check arguments */
	CHECK_DO(proc_if!= NULL, return NULL);
	CHECK_DO(settings_str!= NULL, return NULL);
	// Note: 'log_ctx' is allowed to be NULL

	/* Allocate context structure */
	live555_rtsp_mux_ctx= (live555_rtsp_mux_ctx_t*)calloc(1, sizeof(
			live555_rtsp_mux_ctx_t));
	CHECK_DO(live555_rtsp_mux_ctx!= NULL, goto end);

	/* Get settings structures */
	live555_rtsp_mux_settings_ctx=
			&live555_rtsp_mux_ctx->live555_rtsp_mux_settings_ctx;
	muxers_settings_mux_ctx=
			&live555_rtsp_mux_settings_ctx->muxers_settings_mux_ctx;

	/* Initialize settings to defaults */
	ret_code= live555_rtsp_mux_settings_ctx_init(live555_rtsp_mux_settings_ctx,
			LOG_CTX_GET());
	CHECK_DO(ret_code== STAT_SUCCESS, goto end);

	/* Parse and put given settings */
	ret_code= live555_rtsp_mux_rest_put((proc_ctx_t*)live555_rtsp_mux_ctx,
			settings_str);
	CHECK_DO(ret_code== STAT_SUCCESS, goto end);

    /* **** Initialize the specific Live555 multiplexer resources ****
     * Now that all the parameters are set, we proceed with Live555 specific's.
     */
	ret_code= live555_rtsp_mux_init_given_settings(live555_rtsp_mux_ctx,
			(const muxers_settings_mux_ctx_t*)muxers_settings_mux_ctx,
			LOG_CTX_GET());
	CHECK_DO(ret_code== STAT_SUCCESS, goto end);

	end_code= STAT_SUCCESS;
end:
    if(end_code!= STAT_SUCCESS)
    	live555_rtsp_mux_close((proc_ctx_t**)&live555_rtsp_mux_ctx);
	return (proc_ctx_t*)live555_rtsp_mux_ctx;
}

static int live555_rtsp_mux_init_given_settings(
		live555_rtsp_mux_ctx_t *live555_rtsp_mux_ctx,
		const muxers_settings_mux_ctx_t *muxers_settings_mux_ctx,
		log_ctx_t *log_ctx)
{
	char *stream_session_name;
	int ret_code, end_code= STAT_ERROR;
	int port= 8554;
	LOG_CTX_INIT(log_ctx);

	/* Check arguments */
	CHECK_DO(live555_rtsp_mux_ctx!= NULL, return STAT_ERROR);
	CHECK_DO(muxers_settings_mux_ctx!= NULL, return STAT_ERROR);
	// Note: 'log_ctx' is allowed to be NULL

	/* Initialize generic multiplexing common context structure */
	ret_code= proc_muxer_mux_ctx_init(
			(proc_muxer_mux_ctx_t*)live555_rtsp_mux_ctx, LOG_CTX_GET());
	CHECK_DO(ret_code== STAT_SUCCESS, goto end);

	/* Open Live555 scheduler */
	live555_rtsp_mux_ctx->taskScheduler= BasicTaskScheduler::createNew();
	CHECK_DO(live555_rtsp_mux_ctx->taskScheduler!= NULL, goto end);

	/* Set up Live555 usage environment.
	 * Unfortunate live555 implementation: we *must* initialize
	 * 'liveMediaPriv' and 'groupsockPriv' to NULL to be able to successfully
	 * delete -reclaim()- this class instance while releasing.
	 */
	live555_rtsp_mux_ctx->usageEnvironment= BasicUsageEnvironment::createNew(
			*live555_rtsp_mux_ctx->taskScheduler);
	CHECK_DO(live555_rtsp_mux_ctx->usageEnvironment!= NULL, goto end);
	live555_rtsp_mux_ctx->usageEnvironment->liveMediaPriv= NULL;
	live555_rtsp_mux_ctx->usageEnvironment->groupsockPriv= NULL;

	/* Create the RTSP server */
	port= muxers_settings_mux_ctx->rtsp_port;
	live555_rtsp_mux_ctx->rtspServer= RTSPServer::createNew(
			*live555_rtsp_mux_ctx->usageEnvironment, port, NULL, SERVER_TOUT);
	if(live555_rtsp_mux_ctx->rtspServer== NULL) {
		LOGE("Live555: %s\n",
				live555_rtsp_mux_ctx->usageEnvironment->getResultMsg());
		goto end;
	}

	/* In run-time we will be able to set up each of the possible streams that
	 * can be served by the RTSP server. Each such stream is implemented using
	 * a 'ServerMediaSession' object, plus one or more 'ServerMediaSubsession'
	 * objects for each audio/video sub-stream.
	 * We initially create the 'ServerMediaSession' object. Sub-sessions will
	 * be created/destroyed dynamically.
	 */
	stream_session_name= muxers_settings_mux_ctx->rtsp_streaming_session_name;
	live555_rtsp_mux_ctx->serverMediaSession= ServerMediaSession::createNew(
			*live555_rtsp_mux_ctx->usageEnvironment,
			stream_session_name!= NULL? stream_session_name: "session",
			"n/a", "n/a");
	CHECK_DO(live555_rtsp_mux_ctx->serverMediaSession!= NULL, goto end);
	live555_rtsp_mux_ctx->rtspServer->addServerMediaSession(
			live555_rtsp_mux_ctx->serverMediaSession);

	/* Register ES-MUXER processor type */
	ret_code= procs_module_opt("PROCS_REGISTER_TYPE",
			&proc_if_live555_rtsp_es_mux);
	CHECK_DO(ret_code== STAT_SUCCESS || ret_code== STAT_ECONFLICT, goto end);

	/* At last, launch scheduler thread */
	ret_code= pthread_create(&live555_rtsp_mux_ctx->taskScheduler_thread, NULL,
			taskScheduler_thr, live555_rtsp_mux_ctx);
	CHECK_DO(ret_code== 0, goto end);

	end_code= STAT_SUCCESS;
end:
    if(end_code!= STAT_SUCCESS)
    	live555_rtsp_mux_deinit_except_settings(live555_rtsp_mux_ctx,
    			LOG_CTX_GET());
	return end_code;
}

/**
 * Implements the proc_if_s::close callback.
 * See .proc_if.h for further details.
 */
static void live555_rtsp_mux_close(proc_ctx_t **ref_proc_ctx)
{
	live555_rtsp_mux_ctx_t *live555_rtsp_mux_ctx= NULL;
	LOG_CTX_INIT(NULL);
	LOGD(">>%s\n", __FUNCTION__); //comment-me

	if(ref_proc_ctx== NULL)
		return;

	if((live555_rtsp_mux_ctx= (live555_rtsp_mux_ctx_t*)*ref_proc_ctx)!= NULL) {
		LOG_CTX_SET(((proc_ctx_t*)live555_rtsp_mux_ctx)->log_ctx);

		live555_rtsp_mux_deinit_except_settings(live555_rtsp_mux_ctx,
				LOG_CTX_GET());

		/* Release settings */
		live555_rtsp_mux_settings_ctx_deinit(
				&live555_rtsp_mux_ctx->live555_rtsp_mux_settings_ctx,
				LOG_CTX_GET());

		/* Release context structure */
		free(live555_rtsp_mux_ctx);
		*ref_proc_ctx= NULL;
	}
	LOGD("<<%s\n", __FUNCTION__); //comment-me
}

/**
 * Release RTSP multiplexer at the exception of its settings context
 * structure.
 */
static void live555_rtsp_mux_deinit_except_settings(
		live555_rtsp_mux_ctx_t *live555_rtsp_mux_ctx, log_ctx_t *log_ctx)
{
	void *thread_end_code= NULL;
	proc_muxer_mux_ctx_t *proc_muxer_mux_ctx= NULL; // Do not release
	LOG_CTX_INIT(log_ctx);
	LOGD(">>%s\n", __FUNCTION__); //comment-me

	/* Check arguments */
	if(live555_rtsp_mux_ctx== NULL)
		return;

	/* Get Multiplexer processing common context structure */
	proc_muxer_mux_ctx= (proc_muxer_mux_ctx_t*)live555_rtsp_mux_ctx;

	/* Join Join ES-threads first
	 * - set flag to notify we are exiting processing;
	 * - unblock and close all running ES-processors;
	 * - join thread.
	 */
	((proc_ctx_t*)live555_rtsp_mux_ctx)->flag_exit= 1;
	if(proc_muxer_mux_ctx!= NULL &&
			proc_muxer_mux_ctx->procs_ctx_es_muxers!= NULL)
		procs_close(&proc_muxer_mux_ctx->procs_ctx_es_muxers);
	LOGD("Waiting thread to join... "); // comment-me
	pthread_join(live555_rtsp_mux_ctx->taskScheduler_thread,
			&thread_end_code);
	if(thread_end_code!= NULL) {
		ASSERT(*((int*)thread_end_code)== STAT_SUCCESS);
		free(thread_end_code);
		thread_end_code= NULL;
	}
	LOGD("joined O.K.\n"); // comment-me

	// '&live555_rtsp_mux_ctx->live555_rtsp_mux_settings_ctx' preserved

	/* **** Release the specific Live555 multiplexer resources **** */

	/* Note about 'live555_rtsp_mux_ctx::serverMediaSession': we do not
	 * need to delete ServerMediaSession object before deleting a
	 * RTSPServer, because the RTSPServer destructor will automatically
	 * delete any ServerMediaSession (as ClientConnection and
	 * ClientSession) objects that it manages. Instead, we can just call
	 * 'Medium::close()' on your RTSPServer object.
	 */
	if(live555_rtsp_mux_ctx->rtspServer!= NULL) {
		Medium::close(live555_rtsp_mux_ctx->rtspServer);
		live555_rtsp_mux_ctx->rtspServer= NULL;
	}

	if(live555_rtsp_mux_ctx->usageEnvironment!= NULL) {
		Boolean ret_boolean= live555_rtsp_mux_ctx->usageEnvironment->reclaim();
		ASSERT(ret_boolean== True);
		if(ret_boolean== True)
			live555_rtsp_mux_ctx->usageEnvironment= NULL;
	}

	if(live555_rtsp_mux_ctx->taskScheduler!= NULL) {
		delete live555_rtsp_mux_ctx->taskScheduler;
		live555_rtsp_mux_ctx->taskScheduler= NULL;
	}

	/* De-initialize generic multiplexing common context structure.
	 * Implementation note: Do this after releasing 'rtspServer', as
	 * processors are referenced inside 'rtspServer' media sub-sessions
	 * related resources.
	 */
	proc_muxer_mux_ctx_deinit(proc_muxer_mux_ctx, LOG_CTX_GET());

	// Reserved for future use: release other new variables here...

	LOGD("<<%s\n", __FUNCTION__); //comment-me
}

/**
 * Implements the proc_if_s::process_frame callback.
 * See .proc_if.h for further details.
 */
static int live555_rtsp_mux_process_frame(proc_ctx_t *proc_ctx,
		fifo_ctx_t* iput_fifo_ctx, fifo_ctx_t* oput_fifo_ctx)
{
	int ret_code, end_code= STAT_ERROR;
	live555_rtsp_mux_ctx_t *live555_rtsp_mux_ctx= NULL; // Do not release
	proc_muxer_mux_ctx_t *proc_muxer_mux_ctx= NULL; // DO not release
	procs_ctx_t *procs_ctx_es_muxers= NULL; // Do not release
	proc_frame_ctx_t *proc_frame_ctx_iput= NULL;
	size_t fifo_elem_size= 0;
	LOG_CTX_INIT(NULL);

	/* Check arguments */
	CHECK_DO(proc_ctx!= NULL, return STAT_ERROR);
	CHECK_DO(iput_fifo_ctx!= NULL, return STAT_ERROR);
	CHECK_DO(oput_fifo_ctx!= NULL, return STAT_ERROR);

	LOG_CTX_SET(proc_ctx->log_ctx);

	/* Get multiplexer context */
	live555_rtsp_mux_ctx= (live555_rtsp_mux_ctx_t*)proc_ctx;

	/* Get multiplexer processing common context structure */
	proc_muxer_mux_ctx= (proc_muxer_mux_ctx_t*)live555_rtsp_mux_ctx;

	/* Get elementary streams MUXERS hanlder (PROCS module) */
	procs_ctx_es_muxers= proc_muxer_mux_ctx->procs_ctx_es_muxers;
	CHECK_DO(procs_ctx_es_muxers!= NULL, goto end);

	/* Get input packet from FIFO buffer */
	ret_code= fifo_get(iput_fifo_ctx, (void**)&proc_frame_ctx_iput,
			&fifo_elem_size);
	CHECK_DO(ret_code== STAT_SUCCESS || ret_code== STAT_EAGAIN, goto end);
	if(ret_code== STAT_EAGAIN) {
		/* This means FIFO was unblocked, just go out with EOF status */
		end_code= STAT_EOF;
		goto end;
	}

	/* Multiplex frame */
	ret_code= procs_send_frame(procs_ctx_es_muxers, proc_frame_ctx_iput->es_id,
			proc_frame_ctx_iput);
	CHECK_DO(ret_code== STAT_SUCCESS || ret_code== STAT_EAGAIN, goto end);

	end_code= STAT_SUCCESS;
end:
	if(proc_frame_ctx_iput!= NULL)
		proc_frame_ctx_release(&proc_frame_ctx_iput);
	return end_code;
}

/**
 * Implements the proc_if_s::opt callback.
 * See .proc_if.h for further details.
 */
static int live555_rtsp_mux_opt(proc_ctx_t *proc_ctx, const char *tag,
		va_list arg)
{
#define PROC_ID_STR_FMT "{\"elementary_stream_id\":%d}"
	int ret_code, end_code= STAT_ERROR;
	live555_rtsp_mux_ctx_t *live555_rtsp_mux_ctx= NULL;
	proc_muxer_mux_ctx_t *proc_muxer_mux_ctx= NULL;
	procs_ctx_t *procs_ctx_es_muxers= NULL;
	char *rest_str= NULL;
	char ref_id_str[strlen(PROC_ID_STR_FMT)+ 64];
	LOG_CTX_INIT(NULL);

	/* Check arguments */
	CHECK_DO(proc_ctx!= NULL, return STAT_ERROR);
	CHECK_DO(tag!= NULL, return STAT_ERROR);

	/*  Check that module instance critical section is locked */
	ret_code= pthread_mutex_trylock(&proc_ctx->api_mutex);
	CHECK_DO(ret_code== EBUSY, return STAT_ERROR);

	LOG_CTX_SET(proc_ctx->log_ctx);

	live555_rtsp_mux_ctx= (live555_rtsp_mux_ctx_t*)proc_ctx;
	proc_muxer_mux_ctx= &live555_rtsp_mux_ctx->proc_muxer_mux_ctx;
	procs_ctx_es_muxers= proc_muxer_mux_ctx->procs_ctx_es_muxers;

	if(TAG_IS("PROCS_ID_ES_MUX_REGISTER")) {
		char *p;
		int elementary_stream_id= -1;
		const char *settings_str= va_arg(arg, const char*);
		char **ref_rest_str= va_arg(arg, char**);
		*ref_rest_str= NULL; // Value to return in case of error
		end_code= procs_opt(procs_ctx_es_muxers, "PROCS_POST",
				"live555_rtsp_es_mux", settings_str, &rest_str,
				live555_rtsp_mux_ctx->usageEnvironment,
				live555_rtsp_mux_ctx->serverMediaSession);
		CHECK_DO(end_code== STAT_SUCCESS && rest_str!= NULL, goto end);

		/* Get processor identifier */
		p= strstr(rest_str, "\"proc_id\":");
		CHECK_DO(p!= NULL, goto end);
		p+= strlen("\"proc_id\":");
		elementary_stream_id= atoi(p);
		CHECK_DO(elementary_stream_id>= 0, goto end);

		/* Prepare JSON string to be returned */
		snprintf(ref_id_str, sizeof(ref_id_str), PROC_ID_STR_FMT,
				elementary_stream_id);
		*ref_rest_str= strdup(ref_id_str);
	} else {
		LOGE("Unknown option\n");
		end_code= STAT_ENOTFOUND;
	}

end:
	if(rest_str!= NULL)
		free(rest_str);
	return end_code;
#undef PROC_ID_STR_FMT
}

/**
 * Implements the proc_if_s::rest_put callback.
 * See .proc_if.h for further details.
 */
static int live555_rtsp_mux_rest_put(proc_ctx_t *proc_ctx, const char *str)
{
	int ret_code;
	live555_rtsp_mux_ctx_t *live555_rtsp_mux_ctx= NULL;
	volatile live555_rtsp_mux_settings_ctx_t *
		live555_rtsp_mux_settings_ctx= NULL;
	volatile muxers_settings_mux_ctx_t *muxers_settings_mux_ctx= NULL;
	LOG_CTX_INIT(NULL);

	/* Check arguments */
	CHECK_DO(proc_ctx!= NULL, return STAT_ERROR);
	CHECK_DO(str!= NULL, return STAT_ERROR);

	LOG_CTX_SET(proc_ctx->log_ctx);

	/* Get multiplexer settings contexts */
	live555_rtsp_mux_ctx= (live555_rtsp_mux_ctx_t*)proc_ctx;
	live555_rtsp_mux_settings_ctx=
			&live555_rtsp_mux_ctx->live555_rtsp_mux_settings_ctx;
	muxers_settings_mux_ctx=
			&live555_rtsp_mux_settings_ctx->muxers_settings_mux_ctx;

	/* PUT generic multiplexer settings */
	ret_code= muxers_settings_mux_ctx_restful_put(muxers_settings_mux_ctx, str,
			LOG_CTX_GET());
	if(ret_code!= STAT_SUCCESS)
		return ret_code;


	/* PUT specific multiplexer settings */
	// Reserved for future use

	/* Finally that we have new settings parsed, reset processor */
	live555_rtsp_reset_on_new_settings(proc_ctx, 1, LOG_CTX_GET());

	return STAT_SUCCESS;
}

/**
 * Implements the proc_if_s::rest_get callback.
 * See .proc_if.h for further details.
 */
static int live555_rtsp_mux_rest_get(proc_ctx_t *proc_ctx,
		const proc_if_rest_fmt_t rest_fmt, void **ref_reponse)
{
	int ret_code, end_code= STAT_ERROR;
	live555_rtsp_mux_ctx_t *live555_rtsp_mux_ctx= NULL;
	procs_ctx_t *procs_ctx_es_muxers= NULL; // Do not release
	volatile live555_rtsp_mux_settings_ctx_t *
		live555_rtsp_mux_settings_ctx= NULL;
	volatile muxers_settings_mux_ctx_t *muxers_settings_mux_ctx= NULL;
	cJSON *cjson_rest= NULL, *cjson_settings= NULL, *cjson_es_array= NULL;
	LOG_CTX_INIT(NULL);

	/* Check arguments */
	CHECK_DO(proc_ctx!= NULL, return STAT_ERROR);
	CHECK_DO(rest_fmt< PROC_IF_REST_FMT_ENUM_MAX, return STAT_ERROR);
	CHECK_DO(ref_reponse!= NULL, return STAT_ERROR);

	LOG_CTX_SET(proc_ctx->log_ctx);

	*ref_reponse= NULL;

	/* Create cJSON tree root object */
	cjson_rest= cJSON_CreateObject();
	CHECK_DO(cjson_rest!= NULL, goto end);

	/* JSON string to be returned:
	 * {
	 *     "settings":
	 *     {
	 *         ...
	 *     },
	 *     elementary_streams:
	 *     [
	 *         {...},
	 *         ...
	 *     ]
	 *     ... // reserved for future use
	 * }
	 */

	/* Get multiplexer settings contexts */
	live555_rtsp_mux_ctx= (live555_rtsp_mux_ctx_t*)proc_ctx;
	live555_rtsp_mux_settings_ctx=
			&live555_rtsp_mux_ctx->live555_rtsp_mux_settings_ctx;
	muxers_settings_mux_ctx=
			&live555_rtsp_mux_settings_ctx->muxers_settings_mux_ctx;

	/* GET generic multiplexer settings */
	ret_code= muxers_settings_mux_ctx_restful_get(muxers_settings_mux_ctx,
			&cjson_settings, LOG_CTX_GET());
	CHECK_DO(ret_code== STAT_SUCCESS && cjson_settings!= NULL, goto end);

	/* GET specific multiplexer settings */
	// Reserved for future use: attach to 'cjson_settings' (should be != NULL)

	/* Attach settings object to REST response */
	cJSON_AddItemToObject(cjson_rest, "settings", cjson_settings);
	cjson_settings= NULL; // Attached; avoid double referencing

	/* **** Attach data to REST response **** */

	/* Get ES-processors array REST and attach */
	procs_ctx_es_muxers= ((proc_muxer_mux_ctx_t*)
			live555_rtsp_mux_ctx)->procs_ctx_es_muxers;
	CHECK_DO(procs_ctx_es_muxers!= NULL, goto end);
	ret_code= live555_rtsp_mux_rest_get_es_array(procs_ctx_es_muxers,
			&cjson_es_array, LOG_CTX_GET());
	CHECK_DO(ret_code== STAT_SUCCESS && cjson_es_array!= NULL, goto end);
	cJSON_AddItemToObject(cjson_rest, "elementary_streams", cjson_es_array);
	cjson_es_array= NULL; // Attached; avoid double referencing

	// Reserved for future use
	/* Example:
	 * cjson_aux= cJSON_CreateNumber((double)live555_rtsp_mux_ctx->var1);
	 * CHECK_DO(cjson_aux!= NULL, goto end);
	 * cJSON_AddItemToObject(cjson_rest, "var1_name", cjson_aux);
	 */

	/* Format response to be returned */
	switch(rest_fmt) {
	case PROC_IF_REST_FMT_CHAR:
		/* Print cJSON structure data to char string */
		*ref_reponse= (void*)CJSON_PRINT(cjson_rest);
		CHECK_DO(*ref_reponse!= NULL && strlen((char*)*ref_reponse)> 0,
				goto end);
		break;
	case PROC_IF_REST_FMT_CJSON:
		*ref_reponse= (void*)cjson_rest;
		cjson_rest= NULL; // Avoid double referencing
		break;
	default:
		goto end;
	}

	end_code= STAT_SUCCESS;
end:
	if(cjson_rest!= NULL)
		cJSON_Delete(cjson_rest);
	if(cjson_settings!= NULL)
		cJSON_Delete(cjson_settings);
	if(cjson_es_array!= NULL)
		cJSON_Delete(cjson_es_array);
	return end_code;
}

static int live555_rtsp_mux_rest_get_es_array(procs_ctx_t *procs_ctx_es_muxers,
		cJSON **ref_cjson_es_array, log_ctx_t *log_ctx)
{
	int i, ret_code, procs_num= 0, end_code= STAT_ERROR;
	cJSON *cjson_es_array= NULL, *cjson_procs_rest= NULL,
			*cjson_procs_es_rest= NULL, *cjson_procs_es_rest_settings= NULL;
	cJSON *cjson_procs= NULL, *cjson_aux= NULL; // Do not release
	char *rest_str_aux= NULL, *es_rest_str_aux= NULL;
	LOG_CTX_INIT(log_ctx);

	/* Check arguments */
	CHECK_DO(procs_ctx_es_muxers!= NULL, return STAT_ERROR);
	CHECK_DO(ref_cjson_es_array!= NULL, return STAT_ERROR);

	*ref_cjson_es_array= NULL;

	/* Create ES-processors array REST and attach*/
	cjson_es_array= cJSON_CreateArray();
	CHECK_DO(cjson_es_array!= NULL, goto end);

	/* JSON string to be returned:
	 * [
	 *     {...}, // ES-object JSON
	 *     ...
	 * ]
	 */

	ret_code= procs_opt(procs_ctx_es_muxers, "PROCS_GET", &rest_str_aux);
	CHECK_DO(ret_code== STAT_SUCCESS && rest_str_aux!= NULL, goto end);

	/* Parse to cJSON structure */
	cjson_procs_rest= cJSON_Parse(rest_str_aux);
	CHECK_DO(cjson_procs_rest!= NULL, goto end);

	/* Get ES-processors array */
	cjson_procs= cJSON_GetObjectItem(cjson_procs_rest, "procs");
	CHECK_DO(cjson_procs!= NULL, goto end);
	procs_num= cJSON_GetArraySize(cjson_procs);
	for(i= 0; i< procs_num; i++) {
		int elem_stream_id;
		cJSON *cjson_proc= cJSON_GetArrayItem(cjson_procs, i);
		CHECK_DO(cjson_proc!= NULL, continue);

		cjson_aux= cJSON_GetObjectItem(cjson_proc, "proc_id");
		CHECK_DO(cjson_aux!= NULL, continue);
		elem_stream_id= (int)cjson_aux->valuedouble;

		/* Get ES-processor REST */
		if(es_rest_str_aux!= NULL) {
			free(es_rest_str_aux);
			es_rest_str_aux= NULL;
		}
		ret_code= procs_opt(procs_ctx_es_muxers, "PROCS_ID_GET",
				elem_stream_id, &es_rest_str_aux);
		CHECK_DO(ret_code== STAT_SUCCESS && es_rest_str_aux!= NULL, continue);

		/* Parse ES-processor response to cJSON structure */
		if(cjson_procs_es_rest!= NULL) {
			cJSON_Delete(cjson_procs_es_rest);
			cjson_procs_es_rest= NULL;
		}
		cjson_procs_es_rest= cJSON_Parse(es_rest_str_aux);
		CHECK_DO(cjson_procs_es_rest!= NULL, continue);

		/* Attach elementary stream Id. (== xxx) */
		cjson_aux= cJSON_CreateNumber((double)elem_stream_id);
		CHECK_DO(cjson_aux!= NULL, continue);
		cJSON_AddItemToObject(cjson_procs_es_rest, "elementary_stream_id",
				cjson_aux);

		/* Detach settings from elementary stream REST */
		if(cjson_procs_es_rest_settings!= NULL) {
			cJSON_Delete(cjson_procs_es_rest_settings);
			cjson_procs_es_rest_settings= NULL;
		}
		cjson_procs_es_rest_settings= cJSON_DetachItemFromObject(
				cjson_procs_es_rest, "settings");

		/* Attach elementary stream data to array */
		cJSON_AddItemToArray(cjson_es_array, cjson_procs_es_rest);
		cjson_procs_es_rest= NULL; // Attached; avoid double referencing
	}

	*ref_cjson_es_array= cjson_es_array;
	cjson_es_array= NULL; // Avoid double referencing
	end_code= STAT_SUCCESS;
end:
	if(cjson_es_array!= NULL)
		cJSON_Delete(cjson_es_array);
	if(rest_str_aux!= NULL)
		free(rest_str_aux);
	if(cjson_procs_rest!= NULL)
		cJSON_Delete(cjson_procs_rest);
	if(es_rest_str_aux!= NULL)
		free(es_rest_str_aux);
	if(cjson_procs_es_rest!= NULL)
		cJSON_Delete(cjson_procs_es_rest);
	if(cjson_procs_es_rest_settings!= NULL)
		cJSON_Delete(cjson_procs_es_rest_settings);
	return end_code;
}

/**
 * Initialize specific Live555 RTSP multiplexer settings to defaults.
 * @param live555_rtsp_mux_settings_ctx
 * @param log_ctx
 * @return Status code (STAT_SUCCESS code in case of success, for other code
 * values please refer to .stat_codes.h).
 */
static int live555_rtsp_mux_settings_ctx_init(
		volatile live555_rtsp_mux_settings_ctx_t *live555_rtsp_mux_settings_ctx,
		log_ctx_t *log_ctx)
{
	int ret_code;
	volatile muxers_settings_mux_ctx_t *muxers_settings_mux_ctx= NULL;
	LOG_CTX_INIT(log_ctx);

	/* Check arguments */
	CHECK_DO(live555_rtsp_mux_settings_ctx!= NULL, return STAT_ERROR);

	muxers_settings_mux_ctx=
			&live555_rtsp_mux_settings_ctx->muxers_settings_mux_ctx;

	/* Initialize generic multiplexer settings */
	ret_code= muxers_settings_mux_ctx_init(muxers_settings_mux_ctx);
	if(ret_code!= STAT_SUCCESS)
		return ret_code;

	/* Initialize specific multiplexer settings */
	// Reserved for future use

	return STAT_SUCCESS;
}

/**
 * Release specific Live555 RTSP multiplexer settings (allocated in heap
 * memory).
 * @param live555_rtsp_mux_settings_ctx
 * @param log_ctx
 */
static void live555_rtsp_mux_settings_ctx_deinit(
		volatile live555_rtsp_mux_settings_ctx_t *live555_rtsp_mux_settings_ctx,
		log_ctx_t *log_ctx)
{
	volatile muxers_settings_mux_ctx_t *muxers_settings_mux_ctx= NULL;
	LOG_CTX_INIT(log_ctx);

	/* Check arguments */
	CHECK_DO(live555_rtsp_mux_settings_ctx!= NULL, return);

	muxers_settings_mux_ctx=
			&live555_rtsp_mux_settings_ctx->muxers_settings_mux_ctx;

	/* Release (heap-allocated) generic multiplexer settings */
	muxers_settings_mux_ctx_deinit(muxers_settings_mux_ctx);

	/* Release specific multiplexer settings */
	// Reserved for future use
}

static void* taskScheduler_thr(void *t)
{
	char volatile *watchVariable;
	live555_rtsp_mux_ctx_t* live555_rtsp_mux_ctx= (live555_rtsp_mux_ctx_t*)t;
	int *ref_end_code= NULL;
	UsageEnvironment *usageEnvironment= NULL;
	LOG_CTX_INIT(NULL);

	/* Allocate return context; initialize to a default 'ERROR' value */
	ref_end_code= (int*)malloc(sizeof(int));
	CHECK_DO(ref_end_code!= NULL, return NULL);
	*ref_end_code= STAT_ERROR;

	/* Check arguments */
	CHECK_DO(live555_rtsp_mux_ctx!= NULL, return (void*)ref_end_code);

	LOG_CTX_SET(((proc_ctx_t*)live555_rtsp_mux_ctx)->log_ctx);

	/* Get Live555's usage environment */
	usageEnvironment= live555_rtsp_mux_ctx->usageEnvironment;
	CHECK_DO(live555_rtsp_mux_ctx!= NULL, return (void*)ref_end_code);

	/* Run the scheduler until "exit flag" is set */
	watchVariable= (char volatile*)
			&((proc_ctx_t*)live555_rtsp_mux_ctx)->flag_exit;
	usageEnvironment->taskScheduler().doEventLoop(watchVariable);

	*ref_end_code= STAT_SUCCESS;
	return (void*)ref_end_code;
}

/* **** Elementary stream instance related implementation **** */

static proc_ctx_t* live555_rtsp_es_mux_open(const proc_if_t *proc_if,
		const char *settings_str, log_ctx_t *log_ctx, va_list arg)
{
	int ret_code, end_code= STAT_ERROR;
	live555_rtsp_es_mux_ctx_t *live555_rtsp_es_mux_ctx= NULL;
	volatile live555_rtsp_es_mux_settings_ctx_t *
			live555_rtsp_es_mux_settings_ctx= NULL; // Do not release
	UsageEnvironment *usageEnvironment= NULL; // Do not release
	ServerMediaSession *serverMediaSession= NULL; // Do not release
	LOG_CTX_INIT(log_ctx);

	/* Check arguments */
	CHECK_DO(proc_if!= NULL, return NULL);
	CHECK_DO(settings_str!= NULL, return NULL);
	// Note: 'log_ctx' is allowed to be NULL

	/* Allocate context structure */
	live555_rtsp_es_mux_ctx= (live555_rtsp_es_mux_ctx_t*)calloc(1, sizeof(
			live555_rtsp_es_mux_ctx_t));
	CHECK_DO(live555_rtsp_es_mux_ctx!= NULL, goto end);

	/* Get settings structure */
	live555_rtsp_es_mux_settings_ctx=
			&live555_rtsp_es_mux_ctx->live555_rtsp_es_mux_settings_ctx;

	/* Initialize settings to defaults */
	ret_code= live555_rtsp_es_mux_settings_ctx_init(
			live555_rtsp_es_mux_settings_ctx, LOG_CTX_GET());
	CHECK_DO(ret_code== STAT_SUCCESS, goto end);

	/* Parse and put given settings */
	ret_code= live555_rtsp_es_mux_rest_put((proc_ctx_t*)
			live555_rtsp_es_mux_ctx, settings_str);
	CHECK_DO(ret_code== STAT_SUCCESS, goto end);

	/* **** Initialize specific context structure members **** */

	live555_rtsp_es_mux_ctx->log_ctx= LOG_CTX_GET();

	usageEnvironment= va_arg(arg, UsageEnvironment*);
	CHECK_DO(usageEnvironment!= NULL, goto end);

	serverMediaSession= va_arg(arg, ServerMediaSession*);
	CHECK_DO(serverMediaSession!= NULL, goto end);

	live555_rtsp_es_mux_ctx->taskScheduler=
			&(usageEnvironment->taskScheduler());
	CHECK_DO(live555_rtsp_es_mux_ctx->taskScheduler!= NULL, goto end);

	live555_rtsp_es_mux_ctx->simpleMediaSubsession=
			SimpleMediaSubsession::createNew(*usageEnvironment,
					live555_rtsp_es_mux_settings_ctx->sdp_mimetype);
	CHECK_DO(live555_rtsp_es_mux_ctx->simpleMediaSubsession!= NULL, goto end);

	ret_code= serverMediaSession->addSubsession(
			live555_rtsp_es_mux_ctx->simpleMediaSubsession);
	CHECK_DO(ret_code== True, goto end);

    end_code= STAT_SUCCESS;
 end:
    if(end_code!= STAT_SUCCESS)
    	live555_rtsp_es_mux_close((proc_ctx_t**)&live555_rtsp_es_mux_ctx);
	return (proc_ctx_t*)live555_rtsp_es_mux_ctx;
}

/**
 * Implements the proc_if_s::close callback.
 * See .proc_if.h for further details.
 */
static void live555_rtsp_es_mux_close(proc_ctx_t **ref_proc_ctx)
{
	live555_rtsp_es_mux_ctx_t *live555_rtsp_es_mux_ctx= NULL;
	LOG_CTX_INIT(NULL);
	LOGD(">>%s\n", __FUNCTION__); //comment-me

	if(ref_proc_ctx== NULL)
		return;

	if((live555_rtsp_es_mux_ctx= (live555_rtsp_es_mux_ctx_t*)*ref_proc_ctx)!=
			NULL) {
		LOG_CTX_SET(((proc_ctx_t*)live555_rtsp_es_mux_ctx)->log_ctx);

		/* Release settings */
		live555_rtsp_es_mux_settings_ctx_deinit(
				&live555_rtsp_es_mux_ctx->live555_rtsp_es_mux_settings_ctx,
				LOG_CTX_GET());

		// Reserved for future use: release other new variables here...

		/* Release context structure */
		free(live555_rtsp_es_mux_ctx);
		*ref_proc_ctx= NULL;
	}
	LOGD("<<%s\n", __FUNCTION__); //comment-me
}

/**
 * Implements the proc_if_s::process_frame callback.
 * See .proc_if.h for further details.
 */
static int live555_rtsp_es_mux_process_frame(proc_ctx_t *proc_ctx,
		fifo_ctx_t* iput_fifo_ctx, fifo_ctx_t* oput_fifo_ctx)
{
	int ret_code, end_code= STAT_ERROR;
	live555_rtsp_es_mux_ctx_t *live555_rtsp_es_mux_ctx= NULL; //Do not release
	proc_frame_ctx_t *proc_frame_ctx= NULL;
	size_t fifo_elem_size= 0;
	SimpleMediaSubsession *simpleMediaSubsession= NULL; //Do not release
	LOG_CTX_INIT(NULL);

	/* Check arguments */
	CHECK_DO(proc_ctx!= NULL, return STAT_ERROR);
	CHECK_DO(iput_fifo_ctx!= NULL, return STAT_ERROR);
	CHECK_DO(oput_fifo_ctx!= NULL, return STAT_ERROR);

	LOG_CTX_SET(proc_ctx->log_ctx);

	/* Get multiplexer context */
	live555_rtsp_es_mux_ctx= (live555_rtsp_es_mux_ctx_t*)proc_ctx;

	/* Get input packet from FIFO buffer */
	ret_code= fifo_get(iput_fifo_ctx, (void**)&proc_frame_ctx, &fifo_elem_size);
	CHECK_DO(ret_code== STAT_SUCCESS || ret_code== STAT_EAGAIN, goto end);
	if(ret_code== STAT_EAGAIN) {
		/* This means FIFO was unblocked, just go out with EOF status */
		end_code= STAT_EOF;
		goto end;
	}

	/* Deliver frame to Live555's "framed source" */
	simpleMediaSubsession= (SimpleMediaSubsession*)
					live555_rtsp_es_mux_ctx->simpleMediaSubsession;
	CHECK_DO(simpleMediaSubsession!= NULL, goto end);
	simpleMediaSubsession->deliverFrame(&proc_frame_ctx);

	end_code= STAT_SUCCESS;
end:
	/* If 'deliverFrame()' method did not consume the frame (frame pointer
	 * was not set to NULL), we must release frame and schedule to avoid
	 * CPU-consuming closed loops.
	 */
	if(proc_frame_ctx!= NULL) {
		proc_frame_ctx_release(&proc_frame_ctx);
		schedule();
	}
	return end_code;
}

/**
 * Implements the proc_if_s::rest_put callback.
 * See .proc_if.h for further details.
 */
static int live555_rtsp_es_mux_rest_put(proc_ctx_t *proc_ctx, const char *str)
{
	int flag_is_query, end_code= STAT_ERROR;
	live555_rtsp_es_mux_ctx_t *live555_rtsp_es_mux_ctx= NULL;
	volatile live555_rtsp_es_mux_settings_ctx_t *
			live555_rtsp_es_mux_settings_ctx= NULL;
	char *sdp_mimetype_str= NULL, *rtp_timestamp_freq_str= NULL,
			*bit_rate_estimated_str= NULL;
	cJSON *cjson_rest= NULL, *cjson_aux= NULL;
	LOG_CTX_INIT(NULL);

	/* Check arguments */
	CHECK_DO(proc_ctx!= NULL, return STAT_ERROR);
	CHECK_DO(str!= NULL, return STAT_ERROR);

	LOG_CTX_SET(proc_ctx->log_ctx);

	/* Get FFmpeg video encoder settings contexts */
	live555_rtsp_es_mux_ctx= (live555_rtsp_es_mux_ctx_t*)proc_ctx;
	live555_rtsp_es_mux_settings_ctx=
			&live555_rtsp_es_mux_ctx->live555_rtsp_es_mux_settings_ctx;

	/* **** PUT specific m2v video encoder settings **** */

	/* Guess string representation format (JSON-REST or Query) */
	//LOGD("'%s'\n", str); //comment-me
	flag_is_query= (str[0]=='{' && str[strlen(str)-1]=='}')? 0: 1;

	/* **** Parse RESTful string to get settings parameters **** */

	if(flag_is_query== 1) {

		/* 'sdp_mimetype' */
		sdp_mimetype_str= uri_parser_query_str_get_value("sdp_mimetype", str);
		if(sdp_mimetype_str!= NULL) {
			char *sdp_mimetype;
			CHECK_DO(strlen(sdp_mimetype_str)> 0,
					end_code= STAT_EINVAL; goto end);

			/* Allocate new session name */
			sdp_mimetype= strdup(sdp_mimetype_str);
			CHECK_DO(sdp_mimetype!= NULL, goto end);

			/* Release old session name and set new one */
			if(live555_rtsp_es_mux_settings_ctx->sdp_mimetype!= NULL)
				free(live555_rtsp_es_mux_settings_ctx->sdp_mimetype);
			live555_rtsp_es_mux_settings_ctx->sdp_mimetype= sdp_mimetype;
		}

		/* 'rtp_timestamp_freq' */
		rtp_timestamp_freq_str= uri_parser_query_str_get_value(
				"rtp_timestamp_freq", str);
		if(rtp_timestamp_freq_str!= NULL)
			live555_rtsp_es_mux_settings_ctx->rtp_timestamp_freq=
					atoll(rtp_timestamp_freq_str);
	} else {

		/* In the case string format is JSON-REST, parse to cJSON structure */
		cjson_rest= cJSON_Parse(str);
		CHECK_DO(cjson_rest!= NULL, goto end);

		/* 'sdp_mimetype' */
		cjson_aux= cJSON_GetObjectItem(cjson_rest, "sdp_mimetype");
		if(cjson_aux!= NULL) {
			char *sdp_mimetype;
			CHECK_DO(strlen(cjson_aux->valuestring)> 0,
					end_code= STAT_EINVAL; goto end);

			/* Allocate new session name */
			sdp_mimetype= strdup(cjson_aux->valuestring);
			CHECK_DO(sdp_mimetype!= NULL, goto end);

			/* Release old session name and set new one */
			if(live555_rtsp_es_mux_settings_ctx->sdp_mimetype!= NULL)
				free(live555_rtsp_es_mux_settings_ctx->sdp_mimetype);
			live555_rtsp_es_mux_settings_ctx->sdp_mimetype= sdp_mimetype;
		}

		/* 'rtp_timestamp_freq' */
		cjson_aux= cJSON_GetObjectItem(cjson_rest, "rtp_timestamp_freq");
		if(cjson_aux!= NULL)
			live555_rtsp_es_mux_settings_ctx->rtp_timestamp_freq=
					cjson_aux->valuedouble;
	}

	/* Finally that we have new settings parsed, reset MUXER */
	// Reserved for future use

	end_code= STAT_SUCCESS;
end:
	if(sdp_mimetype_str!= NULL)
		free(sdp_mimetype_str);
	if(rtp_timestamp_freq_str!= NULL)
		free(rtp_timestamp_freq_str);
	if(bit_rate_estimated_str!= NULL)
		free(bit_rate_estimated_str);
	if(cjson_rest!= NULL)
		cJSON_Delete(cjson_rest);
	return end_code;
}

/**
 * Implements the proc_if_s::rest_get callback.
 * See .proc_if.h for further details.
 */
static int live555_rtsp_es_mux_rest_get(proc_ctx_t *proc_ctx,
		const proc_if_rest_fmt_t rest_fmt, void **ref_reponse)
{
	int end_code= STAT_ERROR;
	live555_rtsp_es_mux_ctx_t *live555_rtsp_es_mux_ctx= NULL;
	volatile live555_rtsp_es_mux_settings_ctx_t *
			live555_rtsp_es_mux_settings_ctx= NULL;
	cJSON *cjson_rest= NULL/*, *cjson_settings= NULL // Not used*/;
	cJSON *cjson_aux= NULL; // Do not release
	LOG_CTX_INIT(NULL);

	/* Check arguments */
	CHECK_DO(proc_ctx!= NULL, return STAT_ERROR);
	CHECK_DO(rest_fmt< PROC_IF_REST_FMT_ENUM_MAX, return STAT_ERROR);
	CHECK_DO(ref_reponse!= NULL, return STAT_ERROR);

	LOG_CTX_SET(proc_ctx->log_ctx);

	*ref_reponse= NULL;

	/* Create cJSON tree root object */
	cjson_rest= cJSON_CreateObject();
	CHECK_DO(cjson_rest!= NULL, goto end);

	/* JSON string to be returned:
	 * {
	 *     // "settings":{}, //RAL: Do not expose in current implementation!
	 *     "sdp_mimetype":string,
	 *     "rtp_timestamp_freq":number
	 *     ... // Reserved for future use
	 * }
	 */

	/* Get FFmpeg video encoder settings contexts */
	live555_rtsp_es_mux_ctx= (live555_rtsp_es_mux_ctx_t*)proc_ctx;
	live555_rtsp_es_mux_settings_ctx=
			&live555_rtsp_es_mux_ctx->live555_rtsp_es_mux_settings_ctx;

	/* **** GET specific ES-MUXER settings **** */

	/* Create cJSON settings object */ // Not used
	//cjson_settings= cJSON_CreateObject();
	//CHECK_DO(cjson_settings!= NULL, goto end);

	/* Attach settings object to REST response */ // Not used
	//cJSON_AddItemToObject(cjson_rest, "settings", cjson_settings);
	//cjson_settings= NULL; // Attached; avoid double referencing

	/* **** Attach data to REST response **** */

	/* 'sdp_mimetype' */
	cjson_aux= cJSON_CreateString(
			live555_rtsp_es_mux_settings_ctx->sdp_mimetype);
	CHECK_DO(cjson_aux!= NULL, goto end);
	cJSON_AddItemToObject(cjson_rest, "sdp_mimetype", cjson_aux);

	/* 'rtp_timestamp_freq' */
	cjson_aux= cJSON_CreateNumber((double)
			live555_rtsp_es_mux_settings_ctx->rtp_timestamp_freq);
	CHECK_DO(cjson_aux!= NULL, goto end);
	cJSON_AddItemToObject(cjson_rest, "rtp_timestamp_freq", cjson_aux);

	// Reserved for future use
	/* Example:
	 * cjson_aux= cJSON_CreateNumber((double)live555_rtsp_es_mux_ctx->var1);
	 * CHECK_DO(cjson_aux!= NULL, goto end);
	 * cJSON_AddItemToObject(cjson_rest, "var1_name", cjson_aux);
	 */

	// Reserved for future use: set other data values here...

	/* Format response to be returned */
	switch(rest_fmt) {
	case PROC_IF_REST_FMT_CHAR:
		/* Print cJSON structure data to char string */
		*ref_reponse= (void*)CJSON_PRINT(cjson_rest);
		CHECK_DO(*ref_reponse!= NULL && strlen((char*)*ref_reponse)> 0,
				goto end);
		break;
	case PROC_IF_REST_FMT_CJSON:
		*ref_reponse= (void*)cjson_rest;
		cjson_rest= NULL; // Avoid double referencing
		break;
	default:
		goto end;
	}

	end_code= STAT_SUCCESS;
end:
	//if(cjson_settings!= NULL) // Not used
	//	cJSON_Delete(cjson_settings);
	if(cjson_rest!= NULL)
		cJSON_Delete(cjson_rest);
	return end_code;
}

static int live555_rtsp_es_mux_settings_ctx_init(
		volatile live555_rtsp_es_mux_settings_ctx_t *
		live555_rtsp_es_mux_settings_ctx, log_ctx_t *log_ctx)
{
	LOG_CTX_INIT(log_ctx);

	/* Check arguments */
	CHECK_DO(live555_rtsp_es_mux_settings_ctx!= NULL, return STAT_ERROR);

	live555_rtsp_es_mux_settings_ctx->sdp_mimetype= strdup("n/a");
	CHECK_DO(live555_rtsp_es_mux_settings_ctx->sdp_mimetype!= NULL,
			return STAT_ERROR);
	live555_rtsp_es_mux_settings_ctx->rtp_timestamp_freq= 9000;

	return STAT_SUCCESS;
}

static void live555_rtsp_es_mux_settings_ctx_deinit(
		volatile live555_rtsp_es_mux_settings_ctx_t *
		live555_rtsp_es_mux_settings_ctx, log_ctx_t *log_ctx)
{
	LOG_CTX_INIT(log_ctx);

	/* Check arguments */
	CHECK_DO(live555_rtsp_es_mux_settings_ctx!= NULL, return);

	/* Release ES-MUXER specific settings */
	if(live555_rtsp_es_mux_settings_ctx->sdp_mimetype!= NULL) {
		free(live555_rtsp_es_mux_settings_ctx->sdp_mimetype);
		live555_rtsp_es_mux_settings_ctx->sdp_mimetype= NULL;
	}
}

/* **** So-called "framed-sink class implementation **** */

SimpleRTPSink2::SimpleRTPSink2(UsageEnvironment& env, Groupsock* RTPgs,
		unsigned char rtpPayloadFormat, unsigned rtpTimestampFrequency,
		char const* sdpMediaTypeString, char const* rtpPayloadFormatName,
		unsigned numChannels, Boolean allowMultipleFramesPerPacket,
		Boolean doNormalMBitRule):
				MultiFramedRTPSink(env, RTPgs, rtpPayloadFormat,
						rtpTimestampFrequency, rtpPayloadFormatName,
						numChannels),
				fAllowMultipleFramesPerPacket(allowMultipleFramesPerPacket),
				fSetMBitOnNextPacket(False)
{
  fSDPMediaTypeString= strDup(
		  sdpMediaTypeString== NULL? "unknown": sdpMediaTypeString);
  fSetMBitOnLastFrames= doNormalMBitRule
		  /*&& strcmp(fSDPMediaTypeString, "audio")!= 0*/;
}

SimpleRTPSink2::~SimpleRTPSink2()
{
  delete[] (char*)fSDPMediaTypeString;
}

SimpleRTPSink2* SimpleRTPSink2::createNew(
		UsageEnvironment& env, Groupsock* RTPgs,
		unsigned char rtpPayloadFormat, unsigned rtpTimestampFrequency,
		char const* sdpMediaTypeString, char const* rtpPayloadFormatName,
		unsigned numChannels, Boolean allowMultipleFramesPerPacket,
		Boolean doNormalMBitRule)
{
	return new SimpleRTPSink2(env, RTPgs, rtpPayloadFormat,
			rtpTimestampFrequency, sdpMediaTypeString, rtpPayloadFormatName,
			numChannels, allowMultipleFramesPerPacket, doNormalMBitRule);
}

void SimpleRTPSink2::doSpecialFrameHandling(unsigned fragmentationOffset,
					   unsigned char* frameStart,
					   unsigned numBytesInFrame,
					   struct timeval framePresentationTime,
					   unsigned numRemainingBytes) {
  if(numRemainingBytes== 0) {
    // This packet contains the last (or only) fragment of the frame.
    // Set the RTP 'M' ('marker') bit, if appropriate:
    if(fSetMBitOnLastFrames)
    	setMarkerBit();
  }
  if(fSetMBitOnNextPacket) {
    //An external object asked for the 'M' bit to be set on the next packet:
    setMarkerBit();
    fSetMBitOnNextPacket = False;
  }

  // Important: Also call our base class's doSpecialFrameHandling(),
  // to set the packet's timestamp:
  MultiFramedRTPSink::doSpecialFrameHandling(fragmentationOffset,
		  frameStart, numBytesInFrame, framePresentationTime,
		  numRemainingBytes);
}

Boolean SimpleRTPSink2::frameCanAppearAfterPacketStart(
		unsigned char const* /*frameStart*/, unsigned /*numBytesInFrame*/)
const
{
  return fAllowMultipleFramesPerPacket;
}

char const* SimpleRTPSink2::sdpMediaType() const
{
  return fSDPMediaTypeString;
}

/* **** So-called "framed-source" class implementation **** */

SimpleFramedSource *SimpleFramedSource::createNew(UsageEnvironment& env,
		log_ctx_t *log_ctx)
{
	return new SimpleFramedSource(env, log_ctx);
}

SimpleFramedSource::SimpleFramedSource(UsageEnvironment& env,
		log_ctx_t *log_ctx):
				FramedSource(env),
				m_log_ctx(log_ctx)
{
	fifo_elem_alloc_fxn_t fifo_elem_alloc_fxn= {0};
	LOG_CTX_INIT(m_log_ctx);
	LOGD(">>::SimpleFramedSource\n"); //comment-me

	/* Initialize input FIFO buffer as *NON-BLOCKING* */
	fifo_elem_alloc_fxn.elem_ctx_dup=
			(fifo_elem_ctx_dup_fxn_t*)proc_frame_ctx_dup;
	fifo_elem_alloc_fxn.elem_ctx_release=
			(fifo_elem_ctx_release_fxn_t*)proc_frame_ctx_release;
	m_fifo_ctx= fifo_open(FRAMED_SOURCE_FIFO_SLOTS, FIFO_O_NONBLOCK,
			&fifo_elem_alloc_fxn);
	ASSERT(m_fifo_ctx!= NULL);

	/* We arrange here for our "deliverFrame" member function to be called
	 * whenever the next frame of data becomes available from the device.
	 */
	m_eventTriggerId= envir().taskScheduler().createEventTrigger(deliverFrame0);

	LOGD("<<::SimpleFramedSource\n"); //comment-me
}

SimpleFramedSource::~SimpleFramedSource()
{
	LOGD_CTX_INIT(m_log_ctx);
	LOGD(">>::~SimpleFramedSource\n"); //comment-me

	/* Release input and output FIFO's */
	fifo_close(&m_fifo_ctx);

	/* Reclaim our 'event trigger' */
	envir().taskScheduler().deleteEventTrigger(m_eventTriggerId);
	m_eventTriggerId= 0;

	LOGD("<<::~SimpleFramedSource\n"); //comment-me
}

void SimpleFramedSource::doGetNextFrame()
{
	LOGD_CTX_INIT(m_log_ctx);
	LOGD(">>SimpleFramedSource::doGetNextFrame\n"); //comment-me

	/*//Reserved for future use
	 * Note: If, for some reason, the source device stops being readable
	 * (e.g., it gets closed), then you do the following:
	 */
	if(0 /* the source stops being readable */) {
		handleClosure();
		return;
	}

	/* Directly deliver the next frame of data if is immediately available */
	if(m_fifo_ctx!= NULL && fifo_get_buffer_level(m_fifo_ctx)> 0)
		deliverFrame();


	LOGD("<<SimpleFramedSource::doGetNextFrame\n"); //comment-me
}

void SimpleFramedSource::deliverFrame0(void *simpleFramedSource_opaque)
{
	LOGD_CTX_INIT(NULL);
	LOGD(">>%s\n", __FUNCTION__); //comment-me
	((SimpleFramedSource*)simpleFramedSource_opaque)->deliverFrame();
	LOGD("<<%s\n", __FUNCTION__); //comment-me
}

/**
 * This function is called when new frame data is available from the device.
 * We deliver this data by copying it to the 'downstream' object, using the
 * following parameters (class members):
 * **** 'in' parameters (these should *not* be modified by this function):
 * ** fTo: The frame data is copied to this address.
 * (Note that the variable "fTo" is *not* modified. Instead, the frame data is
 * copied to the address pointed to by "fTo".)
 * ** fMaxSize: This is the maximum number of bytes that can be copied
 * (If the actual frame is larger than this, then it should be truncated,
 * and "fNumTruncatedBytes" set accordingly.)
 * ***** 'out' parameters (these are modified by this function):
 * ** fFrameSize: Should be set to the delivered frame size (<= fMaxSize).
 * ** fNumTruncatedBytes: Should be set iff the delivered frame would have
 * been bigger than "fMaxSize", in which case it's set to the number of bytes
 * that have been omitted.
 * ** fPresentationTime: Should be set to the frame's presentation time
 * (seconds, microseconds). This time must be aligned with 'wall-clock time'
 * -i.e., the time that you would get by calling "gettimeofday()".
 * ** fDurationInMicroseconds: Should be set to the frame's duration, if known.
 * If, however, the device is a 'live source' (e.g., encoded from a camera or
 * microphone), then we probably don't need to set this variable, because
 * -in this case- data will never arrive 'early'.
 */
void SimpleFramedSource::deliverFrame()
{
	uint8_t *newFrame= NULL;
	int ret_code, newFrameSize= 0;
	proc_frame_ctx_t *proc_frame_ctx_show= NULL; //Do not release but modify
	size_t fifo_elem_size= 0;
	LOG_CTX_INIT(m_log_ctx);
	LOGD(">>%s\n", __FUNCTION__); //comment-me

	if(!isCurrentlyAwaitingData()) {
		LOGD("-!isCurrentlyAwaitingData()-"); //comment-me
		goto end; // we're not ready for the data yet
	}

	/* Show (but not consume yet) next packet from FIFO buffer.
	 * The packet, if not fully consumed, will be modified.
	 * The 'ret_code' should always be 'STAT_SUCCESS' as this method should
	 * only be called if there is data available. Also, note that this FIFO is
	 * *NOT* blocking.
	 */
	ret_code= fifo_show(m_fifo_ctx, (void**)&proc_frame_ctx_show,
			&fifo_elem_size);
	CHECK_DO(ret_code== STAT_SUCCESS && proc_frame_ctx_show!= NULL, goto end);

	/* Get input frame pointer and size */
	newFrame= (uint8_t*)proc_frame_ctx_show->p_data[0];
	newFrameSize= proc_frame_ctx_show->width[0];
	if(newFrameSize> (int)fMaxSize) {
		LOGW("Input frame fragmented (Elementary Stream Id.: %d\n)",
				proc_frame_ctx_show->es_id);
		fFrameSize= fMaxSize;
		fNumTruncatedBytes= newFrameSize- fMaxSize;
		/* Update frame pointer and size for next call to deliverFrame() */
		proc_frame_ctx_show->p_data[0]= (const uint8_t*)(newFrame+ fMaxSize);
		proc_frame_ctx_show->width[0]-= fMaxSize;
	} else {
		fFrameSize= newFrameSize;
		fNumTruncatedBytes= 0;
	}
	gettimeofday(&fPresentationTime, NULL); //TODO

	/* Copy frame (or segment) to output buffer */
	memmove(fTo, newFrame, fFrameSize);

	/* After delivering the data, inform the reader that it is now available */
	FramedSource::afterGetting(this);

	/* Consume packet from FIFO if fully used (not truncated) */
	if(fNumTruncatedBytes== 0) {
		proc_frame_ctx_t *proc_frame_ctx= NULL;
		ret_code= fifo_get(m_fifo_ctx, (void**)&proc_frame_ctx,
				&fifo_elem_size);
		ASSERT(ret_code== STAT_SUCCESS);
		proc_frame_ctx_release(&proc_frame_ctx);
	}

end:
	LOGD("<<%s\n", __FUNCTION__); //comment-me
	return;
}

/* **** So-called "media sub-session" class implementation **** */

SimpleMediaSubsession * SimpleMediaSubsession::createNew(UsageEnvironment &env,
		const char *sdp_mimetype, portNumBits initialPortNum,
		Boolean multiplexRTCPWithRTP)
{
	return new SimpleMediaSubsession(env, sdp_mimetype, initialPortNum,
			multiplexRTCPWithRTP);
}

SimpleMediaSubsession::SimpleMediaSubsession(UsageEnvironment &env,
		const char *sdp_mimetype, portNumBits initialPortNum,
		Boolean multiplexRTCPWithRTP):
				OnDemandServerMediaSubsession(env, True/*reuseFirstSource*/,
						initialPortNum, multiplexRTCPWithRTP),
				m_simpleFramedSource(NULL),
				m_log_ctx(NULL)
{
	LOGD_CTX_INIT(m_log_ctx);
	LOGD(">>::SimpleMediaSubsession\n"); //comment-me

	/* Check members initialization */
	if(sdp_mimetype!= NULL && strlen(sdp_mimetype)> 0)
		m_sdp_mimetype= sdp_mimetype;
	else
		m_sdp_mimetype= (const char*)"n/a";

	LOGD("<<::SimpleMediaSubsession\n"); //comment-me
}

void SimpleMediaSubsession::deliverFrame(proc_frame_ctx_t **ref_proc_frame_ctx)
{
	LOG_CTX_INIT(m_log_ctx);

	/* Check arguments */
	CHECK_DO(ref_proc_frame_ctx!= NULL && *ref_proc_frame_ctx!= NULL, return);

	m_simpleFramedSource_mutex.lock();

	if(m_simpleFramedSource!= NULL) {
		int ret_code;

		/* Pass input frame reference to framed-source FIFO */
		ret_code= fifo_put(m_simpleFramedSource->m_fifo_ctx,
				(void**)ref_proc_frame_ctx, sizeof(void*));
		if(ret_code== STAT_ENOMEM)
			LOGW("MUXER buffer overflow: throughput may be exceeding "
					"processing capacity?\n");
		else
			ASSERT(*ref_proc_frame_ctx== NULL);

		/* Notify scheduler that we have a new frame!. Note that
		 * 'm_simpleFramedSource' handler will be available only if a RTSP
		 * session is established with a RTSP client.
		 * If handler is available, we just trigger the corresponding
		 * framed-source event; framed-source will internally read the FIFO
		 * buffer and consume the input frame. If handler is not available
		 * (namely, we have a NULL handler reference), we should consume the
		 * input frame to avoid buffer overflowing (the calling function
		 * consumes it).
		 */
		envir().taskScheduler().triggerEvent(
				m_simpleFramedSource->m_eventTriggerId, m_simpleFramedSource);
	}

	m_simpleFramedSource_mutex.unlock();
	return;
}

/*
 * This method is called internally by the parent class
 * OnDemandServerMediaSubsession.
 */
FramedSource* SimpleMediaSubsession::createNewStreamSource(
		unsigned clientSessionId, unsigned &estBitrate)
{
	FramedSource *framedSource= NULL;
	LOG_CTX_INIT(m_log_ctx);

	LOGD(">> SimpleMediaSubsession::createNewStreamSource\n"); //comment-me

	estBitrate= 3000; /* Kbps */
	OutPacketBuffer::increaseMaxSizeTo(SINK_BUFFER_SIZE);

	/* Instantiate (and initialize) simple framed source */
	framedSource= SimpleFramedSource::createNew(envir(), LOG_CTX_GET());
	m_simpleFramedSource_mutex.lock();
	m_simpleFramedSource= (SimpleFramedSource*)framedSource;
	m_simpleFramedSource_mutex.unlock();

	// Reserved for future use: here instantiate so-called "discrete framer"

	LOGD("<< SimpleMediaSubsession::createNewStreamSource\n"); //comment-me
	return framedSource;
}

void SimpleMediaSubsession::deleteStream(unsigned clientSessionId,
		void*& streamToken)
{
	LOGD_CTX_INIT(m_log_ctx);

	LOGD(">> SimpleMediaSubsession::deleteStream\n"); //comment-me
	m_simpleFramedSource_mutex.lock();
	m_simpleFramedSource= NULL;
	m_simpleFramedSource_mutex.unlock();
	OnDemandServerMediaSubsession::deleteStream(clientSessionId, streamToken);
	LOGD("<< SimpleMediaSubsession::deleteStream\n"); //comment-me
}

void SimpleMediaSubsession::closeStreamSource(FramedSource* inputSource)
{
	LOGD_CTX_INIT(m_log_ctx);
	LOGD(">> SimpleMediaSubsession::closeStreamSource\n"); //comment-me
	m_simpleFramedSource_mutex.lock();
	m_simpleFramedSource= NULL;
	m_simpleFramedSource_mutex.unlock();
	OnDemandServerMediaSubsession::closeStreamSource(inputSource);
	LOGD("<< SimpleMediaSubsession::closeStreamSource\n"); //comment-me
}

RTPSink* SimpleMediaSubsession::createNewRTPSink(Groupsock* rtpGroupsock,
		unsigned char rtpPayloadTypeIfDynamic, FramedSource* inputSource)
{
	char *sdp_p; // Do not release
	RTPSink *rtpSink= NULL;
	char *type_str= NULL, *subtype_str= NULL;
	LOG_CTX_INIT(m_log_ctx);
	LOGD(">>SimpleMediaSubsession::createNewRTPSink\n"); //comment-me

	/* Sanity checks */
	CHECK_DO(m_sdp_mimetype!= NULL, goto end);

	/* Extract MIME type and sub-type */
	sdp_p= strchr((char*)m_sdp_mimetype, '/');
	if(sdp_p!= NULL && strlen(m_sdp_mimetype)> 0)
		type_str= strndup(m_sdp_mimetype, sdp_p- m_sdp_mimetype);
	else
		type_str= strdup("n/a");
	if(sdp_p!= NULL && strlen(sdp_p+ 1)> 0)
		subtype_str= strdup(sdp_p+ 1);
	else
		subtype_str= strdup("n/a");

	/* Select specific RTP-Sink */
	// Reserved for future; e.g.:
	//if(strcmp(sdp_mimetype, "video/LHE")== 0) {
	//	"function pointer"= myRTPSink::createNew;
	//}

	/* Create Sink */
	rtpSink= SimpleRTPSink2::createNew(envir(), rtpGroupsock,
			rtpPayloadTypeIfDynamic, 90000, type_str, subtype_str,
			1,
			False/*allowMultipleFramesPerPacket*/, True /*doNormalMBitRule*/);

end:
	if(type_str!= NULL)
		free(type_str);
	if(subtype_str!= NULL)
		free(subtype_str);
	LOGD("<<SimpleMediaSubsession::createNewRTPSink\n"); //comment-me
	return rtpSink;
}

/* **** De-multiplexer **** */

/**
 * Implements the proc_if_s::open callback.
 * See .proc_if.h for further details.
 */
static proc_ctx_t* live555_rtsp_dmux_open(const proc_if_t *proc_if,
		const char *settings_str, log_ctx_t *log_ctx, va_list arg)
{
	int ret_code, end_code= STAT_ERROR;
	live555_rtsp_dmux_ctx_t *live555_rtsp_dmux_ctx= NULL;
	volatile live555_rtsp_dmux_settings_ctx_t *live555_rtsp_dmux_settings_ctx=
			NULL; // Do not release (alias)
	volatile muxers_settings_dmux_ctx_t *muxers_settings_dmux_ctx=
			NULL; // Do not release (alias)
	LOG_CTX_INIT(log_ctx);

	/* Check arguments */
	CHECK_DO(proc_if!= NULL, return NULL);
	CHECK_DO(settings_str!= NULL, return NULL);
	// Note: 'log_ctx' is allowed to be NULL

	/* Allocate context structure */
	live555_rtsp_dmux_ctx= (live555_rtsp_dmux_ctx_t*)calloc(1, sizeof(
			live555_rtsp_dmux_ctx_t));
	CHECK_DO(live555_rtsp_dmux_ctx!= NULL, goto end);

	/* Get settings structures */
	live555_rtsp_dmux_settings_ctx=
			&live555_rtsp_dmux_ctx->live555_rtsp_dmux_settings_ctx;
	muxers_settings_dmux_ctx=
			&live555_rtsp_dmux_settings_ctx->muxers_settings_dmux_ctx;

	/* Initialize settings to defaults */
	ret_code= live555_rtsp_dmux_settings_ctx_init(
			live555_rtsp_dmux_settings_ctx, LOG_CTX_GET());
	CHECK_DO(ret_code== STAT_SUCCESS, goto end);

	/* Parse and put given settings */
	ret_code= live555_rtsp_dmux_rest_put((proc_ctx_t*)live555_rtsp_dmux_ctx,
			settings_str);
	CHECK_DO(ret_code== STAT_SUCCESS, goto end);

    /* **** Initialize the specific Live555 de-multiplexer resources ****
     * Now that all the parameters are set, we proceed with Live555 specific's.
     */
	ret_code= live555_rtsp_dmux_init_given_settings(live555_rtsp_dmux_ctx,
			(const muxers_settings_dmux_ctx_t*)muxers_settings_dmux_ctx,
			LOG_CTX_GET());
	CHECK_DO(ret_code== STAT_SUCCESS, goto end);

	end_code= STAT_SUCCESS;
end:
    if(end_code!= STAT_SUCCESS)
    	live555_rtsp_dmux_close((proc_ctx_t**)&live555_rtsp_dmux_ctx);
	return (proc_ctx_t*)live555_rtsp_dmux_ctx;
}

static int live555_rtsp_dmux_init_given_settings(
		live555_rtsp_dmux_ctx_t *live555_rtsp_dmux_ctx,
		const muxers_settings_dmux_ctx_t *muxers_settings_dmux_ctx,
		log_ctx_t *log_ctx)
{
	int end_code= STAT_ERROR;
	LOG_CTX_INIT(log_ctx);

	/* Check arguments */
	CHECK_DO(live555_rtsp_dmux_ctx!= NULL, return STAT_ERROR);
	CHECK_DO(muxers_settings_dmux_ctx!= NULL, return STAT_ERROR);
	// Note: 'log_ctx' is allowed to be NULL

	/* Open Live555 scheduler */
	live555_rtsp_dmux_ctx->taskScheduler= BasicTaskScheduler::createNew();
	CHECK_DO(live555_rtsp_dmux_ctx->taskScheduler!= NULL, goto end);

	/* Set up Live555 usage environment. */
	live555_rtsp_dmux_ctx->usageEnvironment= BasicUsageEnvironment::createNew(
			*live555_rtsp_dmux_ctx->taskScheduler);
	CHECK_DO(live555_rtsp_dmux_ctx->usageEnvironment!= NULL, goto end);
	live555_rtsp_dmux_ctx->usageEnvironment->liveMediaPriv= NULL;
	live555_rtsp_dmux_ctx->usageEnvironment->groupsockPriv= NULL;

	end_code= STAT_SUCCESS;
end:
    if(end_code!= STAT_SUCCESS)
    	live555_rtsp_dmux_deinit_except_settings(live555_rtsp_dmux_ctx,
    			LOG_CTX_GET());
	return end_code;
}

/**
 * Implements the proc_if_s::close callback.
 * See .proc_if.h for further details.
 */
static void live555_rtsp_dmux_close(proc_ctx_t **ref_proc_ctx)
{
	live555_rtsp_dmux_ctx_t *live555_rtsp_dmux_ctx= NULL;
	LOG_CTX_INIT(NULL);
	LOGD(">> %s\n", __FUNCTION__); //comment-me

	if(ref_proc_ctx== NULL)
		return;

	if((live555_rtsp_dmux_ctx= (live555_rtsp_dmux_ctx_t*)*ref_proc_ctx)!=
			NULL) {
		LOG_CTX_SET(((proc_ctx_t*)live555_rtsp_dmux_ctx)->log_ctx);

		/* Implementation note: 'live555_rtsp_dmux_process_frame()' is
		 * exited first (and previously,
		 * '((proc_ctx_t*)live555_rtsp_dmux_ctx)->flag_exit' is set to 1
		 * accordingly.
		 */

		/* Release settings */
		live555_rtsp_dmux_settings_ctx_deinit(
				&live555_rtsp_dmux_ctx->live555_rtsp_dmux_settings_ctx,
				LOG_CTX_GET());

		/* **** Release the specific Live555 de-multiplexer resources **** */

		live555_rtsp_dmux_deinit_except_settings(live555_rtsp_dmux_ctx,
				LOG_CTX_GET());

		// Reserved for future use: release other new variables here...
		/* Release context structure */
		free(live555_rtsp_dmux_ctx);
		*ref_proc_ctx= NULL;
	}
	LOGD("<< %s\n", __FUNCTION__); //comment-me
}

static void live555_rtsp_dmux_deinit_except_settings(
		live555_rtsp_dmux_ctx_t *live555_rtsp_dmux_ctx, log_ctx_t *log_ctx)
{
	LOG_CTX_INIT(log_ctx);

	if(live555_rtsp_dmux_ctx== NULL)
		return;

	if(live555_rtsp_dmux_ctx->usageEnvironment!= NULL) {
		Boolean ret_boolean= live555_rtsp_dmux_ctx->usageEnvironment->reclaim();
		ASSERT(ret_boolean== True);
		if(ret_boolean== True)
			live555_rtsp_dmux_ctx->usageEnvironment= NULL;
	}

	if(live555_rtsp_dmux_ctx->taskScheduler!= NULL) {
		delete live555_rtsp_dmux_ctx->taskScheduler;
		live555_rtsp_dmux_ctx->taskScheduler= NULL;
	}
}

/**
 * Implements the proc_if_s::rest_get callback.
 * See .proc_if.h for further details.
 */
static int live555_rtsp_dmux_rest_get(proc_ctx_t *proc_ctx,
		const proc_if_rest_fmt_t rest_fmt, void **ref_reponse)
{
	int end_code= STAT_ERROR;
	live555_rtsp_dmux_ctx_t *live555_rtsp_dmux_ctx= NULL;
	volatile live555_rtsp_dmux_settings_ctx_t *live555_rtsp_dmux_settings_ctx=
			NULL;
	volatile muxers_settings_dmux_ctx_t *muxers_settings_dmux_ctx= NULL;
	SimpleRTSPClient *simpleRTSPClient= NULL;
	MediaSession* session= NULL;
	cJSON *cjson_rest= NULL, *cjson_settings= NULL, *cjson_es_array= NULL,
			*cjson_es= NULL;
	cJSON *cjson_aux= NULL; // Do not release
	char sdp_mimetype_buf[512];
	LOG_CTX_INIT(NULL);

	/* Check arguments */
	CHECK_DO(proc_ctx!= NULL, return STAT_ERROR);
	CHECK_DO(rest_fmt< PROC_IF_REST_FMT_ENUM_MAX, return STAT_ERROR);
	CHECK_DO(ref_reponse!= NULL, return STAT_ERROR);

	LOG_CTX_SET(proc_ctx->log_ctx);

	*ref_reponse= NULL;

	/* Create cJSON tree root object */
	cjson_rest= cJSON_CreateObject();
	CHECK_DO(cjson_rest!= NULL, goto end);

	/* JSON string to be returned:
	 * {
	 *     "settings":
	 *     {
	 *         "rtsp_url":string
	 *     },
	 *     elementary_streams:
	 *     [
	 *         {
	 *             "sdp_mimetype":string,
	 *             "port":number,
	 *             "elementary_stream_id":number
	 *         },
	 *         ...
	 *     ]
	 *     ... // Reserved for future use
	 * }
	 */

	/* Get FFmpeg video encoder settings contexts */
	live555_rtsp_dmux_ctx= (live555_rtsp_dmux_ctx_t*)proc_ctx;
	live555_rtsp_dmux_settings_ctx=
			&live555_rtsp_dmux_ctx->live555_rtsp_dmux_settings_ctx;
	muxers_settings_dmux_ctx=
			&live555_rtsp_dmux_settings_ctx->muxers_settings_dmux_ctx;

	/* **** GET specific ES-MUXER settings **** */

	/* Create cJSON settings object */
	cjson_settings= cJSON_CreateObject();
	CHECK_DO(cjson_settings!= NULL, goto end);

	/* 'rtsp_url' */
	cjson_aux= cJSON_CreateString(muxers_settings_dmux_ctx->rtsp_url);
	CHECK_DO(cjson_aux!= NULL, goto end);
	cJSON_AddItemToObject(cjson_settings, "rtsp_url", cjson_aux);

	/* Attach settings object to REST response */
	cJSON_AddItemToObject(cjson_rest, "settings", cjson_settings);
	cjson_settings= NULL; // Attached; avoid double referencing

	/* **** Attach data to REST response **** */

	/* Create ES-processors array REST and attach*/
	cjson_es_array= cJSON_CreateArray();
	CHECK_DO(cjson_es_array!= NULL, goto end);

	simpleRTSPClient= live555_rtsp_dmux_ctx->simpleRTSPClient;
	CHECK_DO(simpleRTSPClient!= NULL, goto end);

	session= simpleRTSPClient->streamClientState.session;
	if(session!= NULL) {
		MediaSubsession *subsession= NULL;
		MediaSubsessionIterator iter(*session);
		while((subsession= iter.next())!= NULL && proc_ctx->flag_exit== 0) {

			/* Create ES-processor cJSON */
			if(cjson_es!= NULL) {
				cJSON_Delete(cjson_es);
				cjson_es= NULL;
			}
			cjson_es= cJSON_CreateObject();
			CHECK_DO(cjson_es!= NULL, goto end);

			/* 'sdp_mimetype' */
			memset(sdp_mimetype_buf, 0, sizeof(sdp_mimetype_buf));
			snprintf(sdp_mimetype_buf, sizeof(sdp_mimetype_buf), "%s/%s",
					subsession->mediumName(), subsession->codecName());
			cjson_aux= cJSON_CreateString(sdp_mimetype_buf);
			CHECK_DO(cjson_aux!= NULL, goto end);
			cJSON_AddItemToObject(cjson_es, "sdp_mimetype", cjson_aux);

			/* 'port' */
			cjson_aux= cJSON_CreateNumber((double)subsession->clientPortNum());
			CHECK_DO(cjson_aux!= NULL, goto end);
			cJSON_AddItemToObject(cjson_es, "port", cjson_aux);

			/* 'elementary_stream_id' (we use port number as unique Id.) */
			cjson_aux= cJSON_CreateNumber((double)subsession->clientPortNum());
			CHECK_DO(cjson_aux!= NULL, goto end);
			cJSON_AddItemToObject(cjson_es, "elementary_stream_id", cjson_aux);

			/* Attach elementary stream data to array */
			cJSON_AddItemToArray(cjson_es_array, cjson_es);
			cjson_es= NULL; // Attached; avoid double referencing
		}
	}

	/* Attach elementary stream data array to REST response */
	cJSON_AddItemToObject(cjson_rest, "elementary_streams", cjson_es_array);
	cjson_es_array= NULL; // Attached; avoid double referencing

	// Reserved for future use
	/* Example:
	 * cjson_aux= cJSON_CreateNumber((double)live555_rtsp_dmux_ctx->var1);
	 * CHECK_DO(cjson_aux!= NULL, goto end);
	 * cJSON_AddItemToObject(cjson_rest, "var1_name", cjson_aux);
	 */

	// Reserved for future use: set other data values here...

	/* Format response to be returned */
	switch(rest_fmt) {
	case PROC_IF_REST_FMT_CHAR:
		/* Print cJSON structure data to char string */
		*ref_reponse= (void*)CJSON_PRINT(cjson_rest);
		CHECK_DO(*ref_reponse!= NULL && strlen((char*)*ref_reponse)> 0,
				goto end);
		break;
	case PROC_IF_REST_FMT_CJSON:
		*ref_reponse= (void*)cjson_rest;
		cjson_rest= NULL; // Avoid double referencing
		break;
	default:
		goto end;
	}

	end_code= STAT_SUCCESS;
end:
	if(cjson_settings!= NULL)
		cJSON_Delete(cjson_settings);
	if(cjson_rest!= NULL)
		cJSON_Delete(cjson_rest);
	if(cjson_es_array!= NULL)
		cJSON_Delete(cjson_es_array);
	if(cjson_es!= NULL)
		cJSON_Delete(cjson_es);
	return end_code;
}

/**
 * Implements the proc_if_s::process_frame callback.
 * See .proc_if.h for further details.
 */
static int live555_rtsp_dmux_process_frame(proc_ctx_t *proc_ctx,
		fifo_ctx_t* iput_fifo_ctx, fifo_ctx_t* oput_fifo_ctx)
{
	char *rtsp_url;
	char volatile *watchVariable;
	int ret_code, end_code= STAT_ERROR;
	live555_rtsp_dmux_ctx_t *live555_rtsp_dmux_ctx= NULL; // Do not release
	volatile live555_rtsp_dmux_settings_ctx_t *live555_rtsp_dmux_settings_ctx=
			NULL; // Do not release (alias)
	volatile muxers_settings_dmux_ctx_t *muxers_settings_dmux_ctx=
			NULL; // Do not release (alias)
	UsageEnvironment *usageEnvironment= NULL; // Do not release
	LOG_CTX_INIT(NULL);

	/* Check arguments */
	CHECK_DO(proc_ctx!= NULL, return STAT_ERROR);
	CHECK_DO(iput_fifo_ctx!= NULL, return STAT_ERROR);
	CHECK_DO(oput_fifo_ctx!= NULL, return STAT_ERROR);

	LOG_CTX_SET(proc_ctx->log_ctx);

	/* Get multiplexer context */
	live555_rtsp_dmux_ctx= (live555_rtsp_dmux_ctx_t*)proc_ctx;

	/* Get Live555's usage environment */
	usageEnvironment= live555_rtsp_dmux_ctx->usageEnvironment;
	CHECK_DO(usageEnvironment!= NULL, goto end);

	/* Get settings structures */
	live555_rtsp_dmux_settings_ctx=
			&live555_rtsp_dmux_ctx->live555_rtsp_dmux_settings_ctx;
	muxers_settings_dmux_ctx=
			&live555_rtsp_dmux_settings_ctx->muxers_settings_dmux_ctx;

	/* Create a unique "RTSPClient" for the stream that we wish to
	 * receive ("rtsp://" URL).
	 */
	rtsp_url= muxers_settings_dmux_ctx->rtsp_url;
	if(rtsp_url== NULL) {
		LOGE("A valid RTSP URL must be provided\n");
		goto end;
	}
	live555_rtsp_dmux_ctx->simpleRTSPClient= SimpleRTSPClient::createNew(
			*live555_rtsp_dmux_ctx->usageEnvironment, rtsp_url,
			&((proc_ctx_t*)live555_rtsp_dmux_ctx)->flag_exit,
			((proc_ctx_t*)live555_rtsp_dmux_ctx)->fifo_ctx_array[PROC_OPUT],
			0/*No-verbose*/, "n/a"/*application-name*/, 0, LOG_CTX_GET());
	if(live555_rtsp_dmux_ctx->simpleRTSPClient== NULL) {
		LOGE("Failed to create a RTSP client for URL %s: %s\n", rtsp_url,
				live555_rtsp_dmux_ctx->usageEnvironment->getResultMsg());
		goto end;
	}

	/* Send a RTSP "DESCRIBE" command to get a SDP description for the stream.
	 * Note that this command -like all RTSP commands- is sent asynchronously;
	 * we do not block waiting for a response, instead, the following function
	 * call returns immediately, and we handle the RTSP response later, from
	 * within the event loop 'doEventLoop()'.
	 *
	 */
	ret_code= live555_rtsp_dmux_ctx->simpleRTSPClient->sendDescribeCommand(
			continueAfterDESCRIBE);
	if(ret_code== 0) {
		LOGE("Failed to send DESCRIBE to RTSP client for URL %s: %s\n",
				rtsp_url,
				live555_rtsp_dmux_ctx->usageEnvironment->getResultMsg());
		goto end;
	}

	/* Implementation note: In the case of the LIVE555's de-multiplexer
	 * implementation, we use 'live555_rtsp_dmux_process_frame()' just to
	 * execute the task scheduler event loop. We do not read nor write to
	 * input or output FIFOs directly. This is performed synchronously using
	 * the task scheduler registered events.
	 */

	/* Run the scheduler until "exit flag" is set */
	watchVariable= (char volatile*)
			&((proc_ctx_t*)live555_rtsp_dmux_ctx)->flag_exit;
	usageEnvironment->taskScheduler().doEventLoop(watchVariable);

	end_code= STAT_SUCCESS;
end:
	if(live555_rtsp_dmux_ctx->simpleRTSPClient!= NULL)
		shutdownStream(live555_rtsp_dmux_ctx->simpleRTSPClient);
	return end_code;
}

/**
 * Implements the proc_if_s::rest_put callback.
 * See .proc_if.h for further details.
 */
static int live555_rtsp_dmux_rest_put(proc_ctx_t *proc_ctx, const char *str)
{
	int ret_code;
	live555_rtsp_dmux_ctx_t *live555_rtsp_dmux_ctx= NULL;
	volatile live555_rtsp_dmux_settings_ctx_t *
		live555_rtsp_dmux_settings_ctx= NULL;
	volatile muxers_settings_dmux_ctx_t *muxers_settings_dmux_ctx= NULL;
	LOG_CTX_INIT(NULL);

	/* Check arguments */
	CHECK_DO(proc_ctx!= NULL, return STAT_ERROR);
	CHECK_DO(str!= NULL, return STAT_ERROR);

	LOG_CTX_SET(proc_ctx->log_ctx);

	/* Get de-multiplexer settings contexts */
	live555_rtsp_dmux_ctx= (live555_rtsp_dmux_ctx_t*)proc_ctx;
	live555_rtsp_dmux_settings_ctx=
			&live555_rtsp_dmux_ctx->live555_rtsp_dmux_settings_ctx;
	muxers_settings_dmux_ctx=
			&live555_rtsp_dmux_settings_ctx->muxers_settings_dmux_ctx;

	/* PUT generic de-multiplexer settings */
	ret_code= muxers_settings_dmux_ctx_restful_put(muxers_settings_dmux_ctx,
			str, LOG_CTX_GET());
	if(ret_code!= STAT_SUCCESS)
		return ret_code;


	/* PUT specific de-multiplexer settings */
	// Reserved for future use

	/* Finally that we have new settings parsed, reset processor */
	live555_rtsp_reset_on_new_settings(proc_ctx, 0, LOG_CTX_GET());

	return STAT_SUCCESS;
}

/**
 * Initialize specific Live555 RTSP de-multiplexer settings to defaults.
 * @param live555_rtsp_dmux_settings_ctx
 * @param log_ctx
 * @return Status code (STAT_SUCCESS code in case of success, for other code
 * values please refer to .stat_codes.h).
 */
static int live555_rtsp_dmux_settings_ctx_init(
		volatile live555_rtsp_dmux_settings_ctx_t *
		live555_rtsp_dmux_settings_ctx, log_ctx_t *log_ctx)
{
	int ret_code;
	volatile muxers_settings_dmux_ctx_t *muxers_settings_dmux_ctx= NULL;
	LOG_CTX_INIT(log_ctx);

	/* Check arguments */
	CHECK_DO(live555_rtsp_dmux_settings_ctx!= NULL, return STAT_ERROR);

	muxers_settings_dmux_ctx=
			&live555_rtsp_dmux_settings_ctx->muxers_settings_dmux_ctx;

	/* Initialize generic de-multiplexer settings */
	ret_code= muxers_settings_dmux_ctx_init(muxers_settings_dmux_ctx);
	if(ret_code!= STAT_SUCCESS)
		return ret_code;

	/* Initialize specific de-multiplexer settings */
	// Reserved for future use

	return STAT_SUCCESS;
}

/**
 * Release specific Live555 RTSP de-multiplexer settings (allocated in heap
 * memory).
 * @param live555_rtsp_dmux_settings_ctx
 * @param log_ctx
 */
static void live555_rtsp_dmux_settings_ctx_deinit(
		volatile live555_rtsp_dmux_settings_ctx_t *
		live555_rtsp_dmux_settings_ctx, log_ctx_t *log_ctx)
{
	volatile muxers_settings_dmux_ctx_t *muxers_settings_dmux_ctx= NULL;
	LOG_CTX_INIT(log_ctx);

	/* Check arguments */
	CHECK_DO(live555_rtsp_dmux_settings_ctx!= NULL, return);

	muxers_settings_dmux_ctx=
			&live555_rtsp_dmux_settings_ctx->muxers_settings_dmux_ctx;

	/* Release (heap-allocated) generic de-multiplexer settings */
	muxers_settings_dmux_ctx_deinit(muxers_settings_dmux_ctx);

	/* Release specific de-multiplexer settings */
	// Reserved for future use
}

static void continueAfterDESCRIBE(RTSPClient *rtspClient, int resultCode,
		char *resultString)
{
	int end_code= STAT_ERROR;
	UsageEnvironment *usageEnvironment= NULL; // Alias
	StreamClientState *streamClientState= NULL; // Alias
	LOG_CTX_INIT(NULL);

	/* Check arguments */
	CHECK_DO(rtspClient!= NULL, goto end);
	usageEnvironment= &rtspClient->envir();
	streamClientState= &((SimpleRTSPClient*)rtspClient)->streamClientState;
	if(resultCode!= 0) {
		LOGE("[URL: '%s'] Failed to get a SDP description: \n",
				rtspClient->url(), resultString);
		goto end;
	}

	LOG_CTX_SET(((SimpleRTSPClient*)rtspClient)->m_log_ctx);

	/* Inform we got a SDP description.
	 * Create a media session object from this SDP description
	 */
	LOGW("Got a SDP description: %s\n", resultString);
	streamClientState->session = SimpleClientSession::createNew(
			*usageEnvironment, resultString);
	if(streamClientState->session== NULL) {
		LOGE("[URL: '%s'] Failed to create a MediaSession object from the SDP "
				"description: %s\n", rtspClient->url(),
				usageEnvironment->getResultMsg());
		goto end;
	} else if(!streamClientState->session->hasSubsessions()) {
		LOGE("[URL: '%s'] This session has no media subsessions (i.e. no 'm=' "
				"lines)\n", rtspClient->url());
		goto end;
	}

	/* Create and set up our data source objects for the session.
	 * We do this by iterating over the session's 'subsessions', calling
	 * "MediaSubsession::initiate()", and then sending a RTSP "SETUP" command,
	 * on each one (Each 'subsession' will have its own data source).
	 */
	streamClientState->iter= new MediaSubsessionIterator(
			*streamClientState->session);
	setupNextSubsession(rtspClient);

	end_code= STAT_SUCCESS;
end:
	if(end_code!= STAT_SUCCESS)
		shutdownStream(rtspClient); // unrecoverable error occurred
	if(resultString!= NULL)
		delete[] resultString;
	return;
}

/**
 * Iterate through each stream's 'sub-sessions', setting up each one.
 */
static void setupNextSubsession(RTSPClient *rtspClient)
{
	UsageEnvironment *usageEnvironment= NULL; // Alias
	StreamClientState *streamClientState= NULL; // Alias
	MediaSubsession *subsession= NULL; // Alias
	MediaSession *session= NULL; // Alias
	char extra_port_str[8]= {0};
	LOG_CTX_INIT(NULL);

	/* Check arguments */
	CHECK_DO(rtspClient!= NULL, return);

	usageEnvironment= &rtspClient->envir();
	streamClientState= &((SimpleRTSPClient*)rtspClient)->streamClientState;

	streamClientState->subsession= subsession= streamClientState->iter->next();
	if(subsession!= NULL) {
		unsigned short port;

		/* If we can not initialize sub-session, go to the next one */
		if(!subsession->initiate(0)) {
			LOGE("[URL: '%s'] Failed to initiate the sub-session '%s/%s': %s\n",
					rtspClient->url(), subsession->mediumName(),
					subsession->codecName(), usageEnvironment->getResultMsg());
			setupNextSubsession(rtspClient);
			return;
		}

		port= subsession->clientPortNum();
		snprintf(extra_port_str, sizeof(extra_port_str), ", %d", port+ 1);
		LOGW("[URL: '%s'] Initiated the sub-session '%s/%s' (client port[s] "
				"%d%s)\n", rtspClient->url(), subsession->mediumName(),
				subsession->codecName(), port,
				!subsession->rtcpIsMuxed()? extra_port_str: "");

		/* Continue setting up this sub-session by sending a RTSP "SETUP"
		 * command.
		 */
		rtspClient->sendSetupCommand(*subsession, continueAfterSETUP, False,
				False/*use TCP= false*/);
		return;
	}

	/* We've finished setting up all of the subsessions. Now, send a RTSP
	 * "PLAY" command to start the streaming.
	 */
	if((session= streamClientState->session)!= NULL) {
		char* absStartTime_str= session->absStartTime();
		if(absStartTime_str!= NULL) {
			/* Special case: the stream is indexed by 'absolute' time, so
			 * send an appropriate "PLAY" command.
			 */
			rtspClient->sendPlayCommand(*session, continueAfterPLAY,
					absStartTime_str, session->absEndTime());
		} else {
			streamClientState->duration= session->playEndTime()-
					session->playStartTime();
			rtspClient->sendPlayCommand(*session, continueAfterPLAY);
		}
	}
	return;
}

static void continueAfterSETUP(RTSPClient *rtspClient, int resultCode,
		char *resultString)
{
	unsigned short port;
	UsageEnvironment *usageEnvironment= NULL; // Alias
	StreamClientState *streamClientState= NULL; // Alias
	MediaSubsession *subsession= NULL; // Alias
	RTCPInstance *rtcpInstance= NULL; // Alias
	char extra_port_str[8]= {0};
	LOG_CTX_INIT(NULL);

	/* Check arguments */
	CHECK_DO(rtspClient!= NULL, goto end);
	usageEnvironment= &rtspClient->envir();
	streamClientState= &((SimpleRTSPClient*)rtspClient)->streamClientState;
	subsession= streamClientState->subsession;
	CHECK_DO(subsession!= NULL, goto end);
	if(resultCode!= 0) {
		LOGE("[URL: '%s'] Failed to set up the sub-session '%s/%s': %s\n",
				rtspClient->url(), subsession->mediumName(),
				subsession->codecName(), resultString);
		goto end;
	}

	port= subsession->clientPortNum();
	snprintf(extra_port_str, sizeof(extra_port_str), ", %d", port+ 1);
	LOGW("[URL: '%s'] Set up the sub-session '%s/%s' (client port[s] %d%s)\n",
			rtspClient->url(), subsession->mediumName(),
			subsession->codecName(), port,
			!subsession->rtcpIsMuxed()? extra_port_str: "");

    /* Having successfully setup the sub-session, create a data sink for it,
     * and call "startPlaying()" on it (This will prepare the data sink to
     * receive data; the actual flow of data from the client won't start
     * happening until later, after we've sent a RTSP "PLAY" command).
     */
    subsession->sink= DummySink::createNew(*usageEnvironment, *subsession,
    		((SimpleRTSPClient*)rtspClient)->m_fifo_ctx, rtspClient->url(),
			LOG_CTX_GET());
    CHECK_DO(subsession->sink!= NULL, goto end);
    // hack to let sub-session handler functions get the "RTSPClient" from
    // the sub-session
    subsession->miscPtr= rtspClient;
    subsession->sink->startPlaying(*(subsession->readSource()),
    		subsessionAfterPlaying, subsession);

    /* Set handler call if a RTCP "BYE" arrives for this sub-session */
    if((rtcpInstance= subsession->rtcpInstance())!= NULL)
    	rtcpInstance->setByeHandler(subsessionByeHandler, subsession);

end:
	setupNextSubsession(rtspClient); // Set up the next sub-session, if any.
	if(resultString!= NULL)
		delete[] resultString;
	return;
}

static void continueAfterPLAY(RTSPClient *rtspClient, int resultCode,
		char *resultString)
{
	int end_code= STAT_ERROR;
	LOG_CTX_INIT(NULL);

	/* Check arguments */
	CHECK_DO(rtspClient!= NULL, goto end);
	if(resultCode!= 0) {
		LOGE("[URL: '%s'] Failed to start playing session: %s\n",
				rtspClient->url(), resultString);
		goto end;
	}

	LOGW("[URL: '%s'] Started playing session...\n", rtspClient->url());

	end_code= STAT_SUCCESS;
end:
	if(end_code!= STAT_SUCCESS)
		shutdownStream(rtspClient);
	if(resultString!= NULL)
		delete[] resultString;
	return;
}

/**
 * called when a stream's sub-session (e.g., audio or video sub-stream) ends.
 */
static void subsessionAfterPlaying(void *clientData)
{
	MediaSubsession *subsession= NULL;
	RTSPClient *rtspClient= NULL;
	LOG_CTX_INIT(NULL);

	/* Check arguments */
	CHECK_DO(clientData!= NULL, return);

	subsession= (MediaSubsession*)clientData;
	rtspClient= (RTSPClient*)(subsession->miscPtr);

	/* Begin by closing this subsession's stream */
	Medium::close(subsession->sink);
	subsession->sink= NULL;

	/* Check whether *all* sub-sessions' streams have now been closed */
	MediaSession& session= subsession->parentSession();
	MediaSubsessionIterator iter(session);
	while((subsession= iter.next())!= NULL) {
		if(subsession->sink!= NULL) return; // this sub-session is still active
	}

	/* All sub-sessions' streams have now been closed, so shutdown the client */
	shutdownStream(rtspClient);
}

/**
 * Called when a RTCP "BYE" is received for a sub-session.
 */
static void subsessionByeHandler(void *clientData)
{
	MediaSubsession *subsession= NULL;
	RTSPClient *rtspClient= NULL;
	LOG_CTX_INIT(NULL);

	/* Check arguments */
	CHECK_DO(clientData!= NULL, return);

	subsession= (MediaSubsession*)clientData;
	rtspClient= (RTSPClient*)(subsession->miscPtr);

	LOGW("[URL: '%s'] Received RTCP 'BYE' on sub-session '%s/%s'\n",
			rtspClient->url(), subsession->mediumName(),
			subsession->codecName());

	/* Now act as if the subsession had closed */
	subsessionAfterPlaying(subsession);
}

/**
 * Used to shut down and close a stream (including its "RTSPClient" object).
 */
static void shutdownStream(RTSPClient *rtspClient, int exitCode)
{
	StreamClientState *streamClientState= NULL; // Alias
	MediaSession *session= NULL;
	LOG_CTX_INIT(NULL);

	/* Check arguments */
	CHECK_DO(rtspClient!= NULL, return);

	streamClientState= &((SimpleRTSPClient*)rtspClient)->streamClientState;

	/* First, check whether any subsessions have still to be closed */
	if((session= streamClientState->session)!= NULL) {
		Boolean someSubsessionsWereActive= False;
		MediaSubsessionIterator iter(*session);
		MediaSubsession* subsession;

		while((subsession= iter.next())!= NULL) {
			if(subsession->sink!= NULL) {
				RTCPInstance* rtcpInstance;
				Medium::close(subsession->sink);
				subsession->sink= NULL;

				/* If server sends a RTCP "BYE" while handling "TEARDOWN" */
				if((rtcpInstance= subsession->rtcpInstance())!= NULL)
					rtcpInstance->setByeHandler(NULL, NULL);
				someSubsessionsWereActive= True;
			}
		}

		/* Send RTSP "TEARDOWN" command to tell server to shutdown the stream.
		 * Don't bother handling the response to the "TEARDOWN".
		 */
		if(someSubsessionsWereActive)
			rtspClient->sendTeardownCommand(*session, NULL);
	}

	/* Close the stream.
	 * Note that this will also cause this stream's "StreamClientState"
	 * structure to get reclaimed.
	 */
	LOGW("[URL: '%s'] Closing the stream.\n", rtspClient->url());
	if(rtspClient->url()!= NULL)
		Medium::close(rtspClient);
}

SimpleRTSPClient* SimpleRTSPClient::createNew(UsageEnvironment& env,
		char const* rtspURL, volatile int *ref_flag_exit,
		fifo_ctx_t *fifo_ctx, int verbosityLevel,
		char const* applicationName,
		portNumBits tunnelOverHTTPPortNum, log_ctx_t *log_ctx)
{
	return new SimpleRTSPClient(env, rtspURL, ref_flag_exit, fifo_ctx,
			verbosityLevel, applicationName, tunnelOverHTTPPortNum, log_ctx);
}

SimpleRTSPClient::SimpleRTSPClient(UsageEnvironment& env, char const* rtspURL,
		volatile int *ref_flag_exit, fifo_ctx_t *fifo_ctx, int verbosityLevel,
		char const* applicationName, portNumBits tunnelOverHTTPPortNum,
		log_ctx_t *log_ctx):
				RTSPClient(env,rtspURL, verbosityLevel, applicationName,
						tunnelOverHTTPPortNum, -1),
				m_ref_flag_exit(ref_flag_exit),
				m_log_ctx(log_ctx),
				m_fifo_ctx(fifo_ctx)
{
	LOG_CTX_INIT(m_log_ctx);
	ASSERT(fifo_ctx!= NULL);
}

SimpleRTSPClient::~SimpleRTSPClient()
{}

SimpleClientMediaSubsession::SimpleClientMediaSubsession(MediaSession& parent):
		MediaSubsession(parent)
{
}

Boolean SimpleClientMediaSubsession::createSourceObjects(
		int useSpecialRTPoffset)
{
	Boolean doNormalMBitRule= False; // default behavior
	char mimeType[strlen(mediumName())+ strlen(codecName())+ 2];
	snprintf(mimeType, sizeof(mimeType), "%s/%s", mediumName(), codecName());

	fReadSource= fRTPSource= SimpleRTPSource::createNew(env(), fRTPSocket,
			fRTPPayloadFormat, fRTPTimestampFrequency, mimeType,
			(unsigned)useSpecialRTPoffset, doNormalMBitRule);
	return True;
}

SimpleClientSession* SimpleClientSession::createNew(UsageEnvironment& env,
		char const* sdpDescription)
{
	SimpleClientSession* newSession = new SimpleClientSession(env);
	if (newSession != NULL) {
		if (!newSession->initializeWithSDP(sdpDescription)) {
			delete newSession;
			return NULL;
		}
	}
	return newSession;
}

SimpleClientSession::SimpleClientSession(UsageEnvironment& env):
				MediaSession(env)
{
}

MediaSubsession* SimpleClientSession::createNewMediaSubsession()
{
	return new SimpleClientMediaSubsession(*this);
}

StreamClientState::StreamClientState(): iter(NULL), session(NULL),
		subsession(NULL), streamTimerTask(NULL), duration(0.0)
{}

StreamClientState::~StreamClientState()
{
	delete iter;
	if(session!= NULL) {
		/* Delete "session" and un-schedule "streamTimerTask" if applicable */
		session->envir().taskScheduler().unscheduleDelayedTask(streamTimerTask);
		Medium::close(session);
	}
}

DummySink* DummySink::createNew(UsageEnvironment& env,
		MediaSubsession& subsession, fifo_ctx_t *fifo_ctx,
		char const* streamId, log_ctx_t *log_ctx)
{
	return new DummySink(env, subsession, fifo_ctx, streamId, log_ctx);
}

DummySink::DummySink(UsageEnvironment& env, MediaSubsession& subsession,
		fifo_ctx_t *fifo_ctx, char const* streamId, log_ctx_t *log_ctx):
			MediaSink(env),
			fSubsession(subsession),
			m_log_ctx(log_ctx),
			m_fifo_ctx(fifo_ctx),
			m_proc_frame_ctx(NULL)
{
	LOG_CTX_INIT(m_log_ctx);
	fStreamId= strDup(streamId);
	fReceiveBuffer= new u_int8_t[SINK_BUFFER_SIZE];
	ASSERT(m_fifo_ctx!= NULL);
}

DummySink::~DummySink() {
	LOGD_CTX_INIT(m_log_ctx); LOGD(">>%s\n", __FUNCTION__); //comment-me
	m_dummySink_io_mutex.lock();
	if(fReceiveBuffer!= NULL)
		delete[] fReceiveBuffer;
	if(fStreamId!= NULL)
		delete[] fStreamId;
	proc_frame_ctx_release(&m_proc_frame_ctx);
	m_dummySink_io_mutex.unlock();
	LOGD("<<%s\n", __FUNCTION__); //comment-me
}

void DummySink::afterGettingFrame(void* clientData, unsigned frameSize,
		unsigned numTruncatedBytes, struct timeval presentationTime,
		unsigned durationInMicroseconds)
{
	DummySink* sink= (DummySink*)clientData;
	LOG_CTX_INIT(NULL);

	/* Check arguments */
	CHECK_DO(sink!= NULL, return);

	sink->afterGettingFrame(frameSize, numTruncatedBytes, presentationTime,
			durationInMicroseconds);
}

void DummySink::afterGettingFrame(unsigned frameSize,
		unsigned numTruncatedBytes, struct timeval presentationTime,
		unsigned /*durationInMicroseconds*/)
{
	register size_t accumu_size, new_size, new_size_alig;
	int ret_code, m_bit= -1;
	RTPSource *rtpsrc= NULL;
	uint8_t *data= NULL;
	LOG_CTX_INIT(m_log_ctx);
	LOGD(">>%s (frameSize: %d; numTruncatedBytes: %d)\n", __FUNCTION__,
			(int)frameSize, (int)numTruncatedBytes); //comment-me

	/* Check arguments */
	if(frameSize== 0)
		goto end;

	/* Get marker bit */
	rtpsrc= fSubsession.rtpSource();
	if(rtpsrc!= NULL)
		m_bit= rtpsrc->curPacketMarkerBit();

	LOGD("%s/%s\n", fSubsession.mediumName(),
			fSubsession.codecName()); //comment-me

	/* Allocate de-multiplexer output frame if applicable */
	if(m_proc_frame_ctx== NULL) {
		m_proc_frame_ctx= proc_frame_ctx_allocate();
		CHECK_DO(m_proc_frame_ctx!= NULL, goto end);
	}

	/* Complete frame if M bit is set; push frame into output FIFO.
	 * Otherwise, accumulate frame fragment data.
	 */
	accumu_size= m_proc_frame_ctx->width[0];
	new_size= accumu_size+ frameSize;
	if(m_bit== 1) {
		new_size_alig= EXTEND_SIZE_TO_MULTIPLE(new_size, CTX_S_BASE_ALIGN);
		data= (uint8_t*)aligned_alloc(CTX_S_BASE_ALIGN, new_size_alig);
		CHECK_DO(data!= NULL, goto end);
		if(accumu_size> 0 && m_proc_frame_ctx->data!= NULL)
			memcpy(data, m_proc_frame_ctx->data, accumu_size);
		memcpy(&data[accumu_size], fReceiveBuffer, frameSize);
		if(m_proc_frame_ctx->data!= NULL)
			free(m_proc_frame_ctx->data);
		m_proc_frame_ctx->data= data;
		m_proc_frame_ctx->p_data[0]= data;
		data= NULL; // Avoid double referencing
		m_proc_frame_ctx->linesize[0]= new_size_alig;
		m_proc_frame_ctx->width[0]= new_size;
		m_proc_frame_ctx->height[0]= 1; // "1D" data
		m_proc_frame_ctx->proc_sample_fmt= PROC_IF_FMT_UNDEF;
		m_proc_frame_ctx->pts= 0; //FIXME!!
		// We use port as the Id. as is an unique number for each elementary
		// stream (unless, for example, we send a transport layer as MPEG2-TS,
		// but it is also valid -elementary stream de-multiplexing will occur
		// at the MPEG2-TS layer in that case, and the layer provides the
		// means for de-multiplexing).
		m_proc_frame_ctx->es_id= fSubsession.clientPortNum();

		/* Write frame to input FIFO */
		ret_code= fifo_put(m_fifo_ctx, (void**)&m_proc_frame_ctx,
				sizeof(void*));
		if(ret_code== STAT_ENOMEM) {
			/* This FIFO was unblocked, probably we are exiting processing */
			proc_frame_ctx_release(&m_proc_frame_ctx);
			goto end;
		}
		ASSERT(ret_code== STAT_SUCCESS && m_proc_frame_ctx== NULL);
	} else {
		data= (uint8_t*)realloc(m_proc_frame_ctx->data, new_size);
		CHECK_DO(data!= NULL, goto end);
		memcpy(&data[accumu_size], fReceiveBuffer, frameSize);
		m_proc_frame_ctx->data= data;
		data= NULL; // Avoid double referencing
		m_proc_frame_ctx->width[0]= new_size;
	}

end:
	if(data!= NULL)
		free(data);
	/* Then continue, to request the next frame of data */
	continuePlaying();
	LOGD("<<%s\n", __FUNCTION__); //comment-me
	return;
}

Boolean DummySink::continuePlaying()
{
	if(fSource== NULL)
		return False; // sanity check (should not happen)

	/* Request the next frame of data from our input source.
	 * "afterGettingFrame()" will get called later, when it arrives.
	 */
	fSource->getNextFrame(fReceiveBuffer, SINK_BUFFER_SIZE, afterGettingFrame,
			this, onSourceClosure, this);
	return True;
}

void live555_rtsp_reset_on_new_settings(proc_ctx_t *proc_ctx,
		int flag_is_muxer, log_ctx_t *log_ctx)
{
    int ret_code, flag_io_locked= 0, flag_thr_joined= 0;
    void *thread_end_code= NULL;
    LOG_CTX_INIT(log_ctx);

    /* Check arguments */
    CHECK_DO(proc_ctx!= NULL, return);

    /* If processor interface was not set yet, it means this function is being
     * call in processor opening phase, so it must be skipped.
     */
    if(proc_ctx->proc_if== NULL)
    	return;

    /* Firstly, stop processing thread:
     * - Signal processing to end;
     * - Unlock i/o FIFOs;
     * - Lock i/o critical section (to make FIFOs unreachable);
     * - Join the thread.
     * IMPORTANT: *do not* set a jump here (return or goto)
     */
	proc_ctx->flag_exit= 1;
	fifo_set_blocking_mode(proc_ctx->fifo_ctx_array[PROC_IPUT], 0);
	fifo_set_blocking_mode(proc_ctx->fifo_ctx_array[PROC_OPUT], 0);
	fair_lock(proc_ctx->fair_lock_io_array[PROC_IPUT]);
	fair_lock(proc_ctx->fair_lock_io_array[PROC_OPUT]);
	flag_io_locked= 1;
	//LOGV("Waiting thread to join... "); // comment-me
	pthread_join(proc_ctx->proc_thread, &thread_end_code);
	if(thread_end_code!= NULL) {
		ASSERT(*((int*)thread_end_code)== STAT_SUCCESS);
		free(thread_end_code);
		thread_end_code= NULL;
	}
	//LOGV("joined O.K.\n"); // comment-me
	flag_thr_joined= 1;

	/* Empty i/o FIFOs */
	fifo_empty(proc_ctx->fifo_ctx_array[PROC_IPUT]);
	fifo_empty(proc_ctx->fifo_ctx_array[PROC_OPUT]);

	/* Reset processor resources */
	if(flag_is_muxer!= 0) {
		live555_rtsp_reset_on_new_settings_es_mux(proc_ctx, LOG_CTX_GET());
	} else {
		live555_rtsp_reset_on_new_settings_es_dmux(proc_ctx, LOG_CTX_GET());
	}

end:
	/* Restore FIFOs blocking mode if applicable */
	fifo_set_blocking_mode(proc_ctx->fifo_ctx_array[PROC_IPUT], 1);
	fifo_set_blocking_mode(proc_ctx->fifo_ctx_array[PROC_OPUT], 1);

	/* Re-launch PROC thread if applicable */
	if(flag_thr_joined!= 0) {
		proc_ctx->flag_exit= 0;
		ret_code= pthread_create(&proc_ctx->proc_thread, NULL,
				(void*(*)(void*))proc_ctx->start_routine, proc_ctx);
		CHECK_DO(ret_code== 0, goto end);
	}

	/* Unlock i/o critical sections if applicable */
	if(flag_io_locked!= 0) {
		fair_unlock(proc_ctx->fair_lock_io_array[PROC_IPUT]);
		fair_unlock(proc_ctx->fair_lock_io_array[PROC_OPUT]);
	}
	return;
}

void live555_rtsp_reset_on_new_settings_es_mux(proc_ctx_t *proc_ctx,
		log_ctx_t *log_ctx)
{
	char *sdp_mimetype;
    int i, ret_code, procs_num= 0;
    live555_rtsp_mux_ctx_t *live555_rtsp_mux_ctx= NULL; // Do not release
	volatile live555_rtsp_mux_settings_ctx_t *live555_rtsp_mux_settings_ctx=
			NULL; // Do not release
	volatile muxers_settings_mux_ctx_t *muxers_settings_mux_ctx=
			NULL; // Do not release
    cJSON *cjson_es_array= NULL, *cjson_aux= NULL;
    char *rest_str= NULL;
    LOG_CTX_INIT(log_ctx);

    /* Check arguments */
    CHECK_DO(proc_ctx!= NULL, return);

    live555_rtsp_mux_ctx= (live555_rtsp_mux_ctx_t*)proc_ctx;

	/* Get settings structures */
	live555_rtsp_mux_settings_ctx=
			&live555_rtsp_mux_ctx->live555_rtsp_mux_settings_ctx;
	muxers_settings_mux_ctx=
			&live555_rtsp_mux_settings_ctx->muxers_settings_mux_ctx;

	/* Get ES-processors array REST (will need to register ES's again) */
	ret_code= live555_rtsp_mux_rest_get_es_array(
			((proc_muxer_mux_ctx_t*)live555_rtsp_mux_ctx)->procs_ctx_es_muxers,
			&cjson_es_array, LOG_CTX_GET());
	CHECK_DO(ret_code== STAT_SUCCESS && cjson_es_array!= NULL, goto end);

	/* Release RTSP multiplexer at the exception of its settings context
	 * structure.
	 */
	live555_rtsp_mux_deinit_except_settings(live555_rtsp_mux_ctx,
			LOG_CTX_GET());

    /* Re-initialize the specific Live555 multiplexer resources */
	ret_code= live555_rtsp_mux_init_given_settings(live555_rtsp_mux_ctx,
			(const muxers_settings_mux_ctx_t*)muxers_settings_mux_ctx,
			LOG_CTX_GET());
	CHECK_DO(ret_code== STAT_SUCCESS, goto end);

	/* Register ES's again */
	procs_num= cJSON_GetArraySize(cjson_es_array);
	for(i= 0; i< procs_num; i++) {
		char settings_str[128]= {0};
		cJSON *cjson_proc= cJSON_GetArrayItem(cjson_es_array, i);
		CHECK_DO(cjson_proc!= NULL, continue);

		cjson_aux= cJSON_GetObjectItem(cjson_proc, "sdp_mimetype");
		CHECK_DO(cjson_aux!= NULL, continue);
		sdp_mimetype= cjson_aux->valuestring;

		/* Register ES */
		if(rest_str!= NULL) {
			free(rest_str);
			rest_str= NULL;
		}
		snprintf(settings_str, sizeof(settings_str), "sdp_mimetype=%s",
				sdp_mimetype);
		ret_code= procs_opt(
				((proc_muxer_mux_ctx_t*)live555_rtsp_mux_ctx)->
				procs_ctx_es_muxers, "PROCS_POST",
				"live555_rtsp_es_mux", settings_str, &rest_str,
				live555_rtsp_mux_ctx->usageEnvironment,
				live555_rtsp_mux_ctx->serverMediaSession);
		CHECK_DO(ret_code== STAT_SUCCESS && rest_str!= NULL, continue);
	}

end:
	if(cjson_es_array!= NULL)
		cJSON_Delete(cjson_es_array);
	if(rest_str!= NULL)
		free(rest_str);
	return;
}

void live555_rtsp_reset_on_new_settings_es_dmux(proc_ctx_t *proc_ctx,
		log_ctx_t *log_ctx)
{
    int ret_code;
    live555_rtsp_dmux_ctx_t *live555_rtsp_dmux_ctx= NULL; // Do not release
	volatile live555_rtsp_dmux_settings_ctx_t *live555_rtsp_dmux_settings_ctx=
			NULL; // Do not release
	volatile muxers_settings_dmux_ctx_t *muxers_settings_dmux_ctx=
			NULL; // Do not release
    LOG_CTX_INIT(log_ctx);

    /* Check arguments */
    CHECK_DO(proc_ctx!= NULL, return);

    live555_rtsp_dmux_ctx= (live555_rtsp_dmux_ctx_t*)proc_ctx;

	/* Get settings structures */
	live555_rtsp_dmux_settings_ctx=
			&live555_rtsp_dmux_ctx->live555_rtsp_dmux_settings_ctx;
	muxers_settings_dmux_ctx=
			&live555_rtsp_dmux_settings_ctx->muxers_settings_dmux_ctx;

	/* Release RTSP de-multiplexer at the exception of its settings context
	 * structure.
	 */
	live555_rtsp_dmux_deinit_except_settings(live555_rtsp_dmux_ctx,
			LOG_CTX_GET());

    /* Re-initialize the specific Live555 multiplexer resources */
	ret_code= live555_rtsp_dmux_init_given_settings(live555_rtsp_dmux_ctx,
			(const muxers_settings_dmux_ctx_t*)muxers_settings_dmux_ctx,
			LOG_CTX_GET());
	ASSERT(ret_code== STAT_SUCCESS);

	return;
}
