//
//

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#ifdef	_MSC_VER
#include <windows.h>
#include <stdint.h>
#pragma warning(disable: 4996)
#include "getopt.h"
#define inline	__inline
#define snprintf	_snprintf
#else
#include <linux/limits.h>
#define MAX_PATH	PATH_MAX
#include <dirent.h>
#include <getopt.h>
#endif


#include <libavutil/avassert.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libavutil/mathematics.h>
#include <libavutil/timestamp.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>

#define WIDTH	640
#define	HEIGHT	480
#define STREAM_DURATION   10.0
#define STREAM_FRAME_RATE 30 /* 30 images/s */
#define STREAM_FRAME_INTERVAL	15
#define STREAM_PIX_FMT    AV_PIX_FMT_YUV420P /* default pix_fmt */
#define SCALE_FLAGS SWS_BICUBIC

// a wrapper around a single output AVStream
typedef struct OutputStream {
	int width;
	int height;
	AVStream *st;
    AVFrame *frame;
    AVFrame *tmp_frame;
    struct SwsContext *sws_ctx;
    struct SwrContext *swr_ctx;
    int64_t next_pts; /* pts of the next frame that will be generated */
} OutputStream;


AVFrame *get_dummy_frame(OutputStream *stream);
AVFrame *read_image_frame(OutputStream *stream, const char *filename);

void open_video(AVFormatContext *context, OutputStream *stream, AVCodec *codec, AVDictionary *opt_arg);
void add_stream(AVFormatContext *context, OutputStream *stream, AVCodec **codec, enum AVCodecID codec_id);
int write_video_frame(AVFormatContext *context, OutputStream *stream, AVFrame *frame);
void close_stream(AVFormatContext *context, OutputStream *stream);

AVFormatContext* open_input_file(const char *filename);
AVFormatContext* open_output_file(const char *filename, AVFormatContext *context);

const char * prepare(AVFormatContext **context, OutputStream *stream, const char *filename);
void finalize(AVFormatContext *context, OutputStream *stream);