//
//

#include "mp4grab.h"
#include <opencv2/core/core_c.h>
#include <opencv2/imgproc/types_c.h>
#include <opencv2/imgproc/imgproc_c.h>
#include <opencv2/highgui/highgui_c.h>

#ifdef _MSC_VER
#pragma comment(lib, "avcodec.lib")
#pragma comment(lib, "avformat.lib")
#pragma comment(lib, "avutil.lib")
#pragma comment(lib, "swscale.lib")
#pragma comment(lib, "swresample.lib")
#	ifdef _DEBUG
#pragma comment(lib, "opencv_core2410d.lib")
#pragma comment(lib, "opencv_highgui2410d.lib")
#pragma comment(lib, "opencv_imgproc2410d.lib")
#	else
#pragma comment(lib, "opencv_core2410.lib")
#pragma comment(lib, "opencv_highgui2410.lib")
#pragma comment(lib, "opencv_imgproc2410.lib")
#	endif
#else
#include <libgen.h>
#endif

static inline AVRational av_create_rational(int num, int den){
	AVRational ret = { num, den };
	return ret;
}

static int write_frame(AVFormatContext *context, const AVRational *time_base, AVStream *stream, AVPacket *packet)
{
    /* rescale output packet timestamp values from codec to stream timebase */
    av_packet_rescale_ts(packet, *time_base, stream->time_base);
    packet->stream_index = stream->index;

    /* Write the compressed frame to the media file. */
    return av_interleaved_write_frame(context, packet);
}


/**************************************************************/
/* video output */
static AVFrame *alloc_picture(enum AVPixelFormat pix_fmt, int width, int height)
{
    AVFrame *picture;
    int ret;
    picture = av_frame_alloc();
    if (!picture)
        return NULL;
    picture->format = pix_fmt;
    picture->width  = width;
    picture->height = height;
    /* allocate the buffers for the frame data */
    ret = av_frame_get_buffer(picture, 32);
    if (ret < 0) {
        fprintf(stderr, "Could not allocate frame data.\n");
        exit(1);
    }
    return picture;
}

// read image and prepare to write(resize and convert to YUV420)
static int ReadImage(AVCodecContext *context, OutputStream *stream, const char *filename)	
{
	AVFrame *frame = stream->frame;
	int x, y;
	IplImage *image;
	IplImage *scaled;
	IplImage *yuv;
	IplImage *img_planes[3] = { NULL, NULL, NULL, };
	
	image = cvLoadImage(filename, 1);
	
	// resize
	scaled = cvCreateImage(cvSize(stream->width, stream->height), image->depth, image->nChannels);
	cvResize(image, scaled, CV_INTER_LINEAR);

	// change color space
	yuv = cvCreateImage(cvSize(context->width, context->height), image->depth, image->nChannels);
	cvCvtColor(scaled, yuv, CV_BGR2YCrCb);

	// separate channel
	img_planes[0] = cvCreateImage(cvSize(context->width, context->height), image->depth, 1);
	img_planes[1] = cvCreateImage(cvSize(context->width, context->height), image->depth, 1);
	img_planes[2] = cvCreateImage(cvSize(context->width, context->height), image->depth, 1);
	cvSplit(yuv, img_planes[0], img_planes[1], img_planes[2], NULL);
		
	// copy data
    for (y = 0; y < context->height; y++) {
		// Y
		for (x = 0; x < context->width; x++) {
			frame->data[0][y * frame->linesize[0] + x] = img_planes[0]->imageData[y * img_planes[0]->widthStep + x];
		}
    }
    for (y = 0; y < context->height / 2; y++) {
		// Cb
		// Cr
		for (x = 0; x < context->width / 2; x++) {
			frame->data[1][(y) * frame->linesize[1] + (x)] = img_planes[2]->imageData[(y * 2) * img_planes[2]->widthStep + (x * 2)];
			frame->data[2][(y) * frame->linesize[2] + (x)] = img_planes[1]->imageData[(y * 2) * img_planes[1]->widthStep + (x * 2)];
		}
	}

	// release images
	cvReleaseImage(&image);
	cvReleaseImage(&scaled);
	cvReleaseImage(&yuv);
	cvReleaseImage(&img_planes[0]);
	cvReleaseImage(&img_planes[1]);
	cvReleaseImage(&img_planes[2]);
	return 0;
}

AVFrame *read_image_frame(OutputStream *stream, const char *filename)
{
	AVCodecContext *context = stream->st->codec;
	ReadImage(context, stream, filename);
	stream->next_pts += 1;
	stream->frame->pts = stream->next_pts;
    return stream->frame;
}

// get duration of movie file
static int64_t getDuration(const char *filename)
{
	int64_t duration = -1;
	AVFormatContext* context = avformat_alloc_context();
	if (context != NULL)
	{
		avformat_open_input(&context, filename, NULL, NULL);
		if (context != NULL)
		{
			avformat_find_stream_info(context,NULL);
			duration = context->duration;
			printf("%d, %d\n", duration, context->start_time);
		// etc
			avformat_close_input(&context);
			avformat_free_context(context);
		}
	}
	return duration;
}

/* Add an output stream. */
static void add_stream(AVFormatContext *context, OutputStream *stream, 
                       AVCodec **codec,
                       enum AVCodecID codec_id)
{
    AVCodecContext *c;
    /* find the encoder */
    *codec = avcodec_find_encoder(codec_id);
    if (!(*codec)) {
        fprintf(stderr, "Could not find encoder for '%s'\n",
                avcodec_get_name(codec_id));
        exit(1);
    }
    stream->st = avformat_new_stream(context, *codec);
    if (!stream->st) {
        fprintf(stderr, "Could not allocate stream\n");
        exit(1);
    }
    stream->st->id = context->nb_streams-1;
    c = stream->st->codec;
    switch ((*codec)->type) {
	/*
    case AVMEDIA_TYPE_AUDIO:
        c->sample_fmt  = (*codec)->sample_fmts ?
            (*codec)->sample_fmts[0] : AV_SAMPLE_FMT_FLTP;
        c->bit_rate    = 64000;
        c->sample_rate = 44100;
        if ((*codec)->supported_samplerates) {
		    int i;
            c->sample_rate = (*codec)->supported_samplerates[0];
            for (i = 0; (*codec)->supported_samplerates[i]; i++) {
                if ((*codec)->supported_samplerates[i] == 44100)
                    c->sample_rate = 44100;
            }
        }
        c->channels        = av_get_channel_layout_nb_channels(c->channel_layout);
        c->channel_layout = AV_CH_LAYOUT_STEREO;
        if ((*codec)->channel_layouts) {
		    int i;
            c->channel_layout = (*codec)->channel_layouts[0];
            for (i = 0; (*codec)->channel_layouts[i]; i++) {
                if ((*codec)->channel_layouts[i] == AV_CH_LAYOUT_STEREO)
                    c->channel_layout = AV_CH_LAYOUT_STEREO;
            }
        }
        c->channels        = av_get_channel_layout_nb_channels(c->channel_layout);
		stream->st->time_base = av_create_rational( 1, c->sample_rate );
        break;
	*/
    case AVMEDIA_TYPE_VIDEO:
        c->codec_id = codec_id;
        c->bit_rate = 400000;
        /* Resolution must be a multiple of two. */
		c->width    = stream->width;
		c->height   = stream->height;
        /* timebase: This is the fundamental unit of time (in seconds) in terms
         * of which frame timestamps are represented. For fixed-fps content,
         * timebase should be 1/framerate and timestamp increments should be
         * identical to 1. */
		stream->st->time_base = av_create_rational( STREAM_FRAME_INTERVAL, STREAM_FRAME_RATE);
        c->time_base       = stream->st->time_base;
        c->gop_size      = 12; /* emit one intra frame every twelve frames at most */
        c->pix_fmt       = STREAM_PIX_FMT;
		c->max_b_frames = 1;
        if (c->codec_id == AV_CODEC_ID_MPEG2VIDEO) {
            /* just for testing, we also add B frames */
            c->max_b_frames = 2;
        }
        if (c->codec_id == AV_CODEC_ID_MPEG1VIDEO) {
            /* Needed to avoid using macroblocks in which some coeffs overflow.
             * This does not happen with normal video, it just happens here as
             * the motion of the chroma plane does not match the luma plane. */
            c->mb_decision = 2;
        }
    break;
    default:
        break;
    }
    /* Some formats want stream headers to be separate. */
    if (context->oformat->flags & AVFMT_GLOBALHEADER)
        c->flags |= CODEC_FLAG_GLOBAL_HEADER;
}

static void open_video(AVFormatContext *context, OutputStream *stream, AVCodec *codec, AVDictionary *opt_arg)
{
    int ret;
    AVCodecContext *c = stream->st->codec;
    AVDictionary *opt = NULL;
    av_dict_copy(&opt, opt_arg, 0);
    /* open the codec */
    ret = avcodec_open2(c, codec, &opt);
    av_dict_free(&opt);
    if (ret < 0) {
        fprintf(stderr, "Could not open video codec: %s\n", av_err2str(ret));
        exit(1);
    }
    /* allocate and init a re-usable frame */
    stream->frame = alloc_picture(c->pix_fmt, c->width, c->height);
    if (!stream->frame) {
        fprintf(stderr, "Could not allocate video frame\n");
        exit(1);
    }
    /* If the output format is not YUV420P, then a temporary YUV420P
     * picture is needed too. It is then converted to the required
     * output format. */
    stream->tmp_frame = NULL;
    if (c->pix_fmt != AV_PIX_FMT_YUV420P) {
        stream->tmp_frame = alloc_picture(AV_PIX_FMT_YUV420P, c->width, c->height);
        if (!stream->tmp_frame) {
            fprintf(stderr, "Could not allocate temporary picture\n");
            exit(1);
        }
    }
}

static int output(AVFormatContext **context, OutputStream *stream, const char *filename)
{
	int ret;
	AVCodec *codec;
	AVDictionary *opt = NULL;
	
	/* allocate the output media context */
    avformat_alloc_output_context2(context, NULL, NULL, filename);
    if (!context) {
        printf("Could not deduce output format from file extension: using MPEG.\n");
        avformat_alloc_output_context2(context, NULL, "mpeg", filename);
    }
    if (!context)
        return 0;
    
	/* Add the audio and video streams using the default format codecs
     * and initialize the codecs. */
    if ((*context)->oformat->video_codec != AV_CODEC_ID_NONE) {
        add_stream(*context, stream, &codec, (*context)->oformat->video_codec);
    }

    /* Now that all the parameters are set, we can open the audio and
     * video codecs and allocate the necessary encode buffers. */
	open_video(*context, stream, codec, opt);

    av_dump_format(*context, 0, filename, 1);

    /* open the output file, if needed */
    if (!((*context)->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&(*context)->pb, filename, AVIO_FLAG_READ_WRITE);
        if (ret < 0) {
            fprintf(stderr, "Could not open '%s': %s\n", filename,
                    av_err2str(ret));
            return 0;
        }
    }

	/* Write the stream header, if any. */
	ret = avformat_write_header(*context, &opt);
	if (ret < 0) {
		fprintf(stderr, "Error occurred when opening output file: %s\n",
				av_err2str(ret));
		return 0;
	}
	return 1;
}

/////////////////////////////////////////////////////////////////////////////
AVFormatContext * open_input_file(const char *filename)
{
    int ret;
    unsigned int i;

    AVFormatContext *ifmt_ctx = NULL;
    if ((ret = avformat_open_input(&ifmt_ctx, filename, NULL, NULL)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot open input file\n");
        return NULL;
    }

    if ((ret = avformat_find_stream_info(ifmt_ctx, NULL)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot find stream information\n");
        return NULL;
    }

    for (i = 0; i < ifmt_ctx->nb_streams; i++) {
        AVStream *stream;
        AVCodecContext *codec_ctx;
        stream = ifmt_ctx->streams[i];
        codec_ctx = stream->codec;
        /* Reencode video & audio and remux subtitles etc. */
        if (codec_ctx->codec_type == AVMEDIA_TYPE_VIDEO
                || codec_ctx->codec_type == AVMEDIA_TYPE_AUDIO) {
            /* Open decoder */
            ret = avcodec_open2(codec_ctx,
                    avcodec_find_decoder(codec_ctx->codec_id), NULL);
            if (ret < 0) {
                av_log(NULL, AV_LOG_ERROR, "Failed to open decoder for stream #%u\n", i);
                return NULL;
            }
        }
    }

    av_dump_format(ifmt_ctx, 0, filename, 0);
    return ifmt_ctx;
}

AVFormatContext * open_output_file(const char *filename, AVFormatContext *ifmt_ctx)
{
    AVStream *out_stream;
    AVStream *in_stream;
    AVCodecContext *dec_ctx, *enc_ctx;
    AVCodec *encoder;
    int ret;
    unsigned int i;

    AVFormatContext *ofmt_ctx = NULL;
    avformat_alloc_output_context2(&ofmt_ctx, NULL, NULL, filename);
    if (!ofmt_ctx) {
        av_log(NULL, AV_LOG_ERROR, "Could not create output context\n");
        return NULL;
    }


    for (i = 0; i < ifmt_ctx->nb_streams; i++) {
        in_stream = ifmt_ctx->streams[i];
        dec_ctx = in_stream->codec;

        if (dec_ctx->codec_type == AVMEDIA_TYPE_VIDEO
                || dec_ctx->codec_type == AVMEDIA_TYPE_AUDIO) {
            /* in this example, we choose transcoding to same codec */
            encoder = avcodec_find_encoder(dec_ctx->codec_id);
            if (!encoder) {
                av_log(NULL, AV_LOG_FATAL, "Neccessary encoder not found\n");
                return NULL;
            }
	        out_stream = avformat_new_stream(ofmt_ctx, encoder);
		    if (!out_stream) {
			    av_log(NULL, AV_LOG_ERROR, "Failed allocating output stream\n");
				return NULL;
			}
			enc_ctx = out_stream->codec;

            /* In this example, we transcode to same properties (picture size,
             * sample rate etc.). These properties can be changed for output
             * streams easily using filters */
            if (dec_ctx->codec_type == AVMEDIA_TYPE_VIDEO) {
                enc_ctx->height = dec_ctx->height;
                enc_ctx->width = dec_ctx->width;
                enc_ctx->sample_aspect_ratio = dec_ctx->sample_aspect_ratio;
                /* take first format from list of supported formats */
                enc_ctx->pix_fmt = encoder->pix_fmts[0];
                /* video time_base can be set to whatever is handy and supported by encoder */
                enc_ctx->time_base = dec_ctx->time_base;
            } else {
                enc_ctx->sample_rate = dec_ctx->sample_rate;
                enc_ctx->channel_layout = dec_ctx->channel_layout;
                enc_ctx->channels = av_get_channel_layout_nb_channels(enc_ctx->channel_layout);
                /* take first format from list of supported formats */
                enc_ctx->sample_fmt = encoder->sample_fmts[0];
                enc_ctx->time_base = av_create_rational(1, enc_ctx->sample_rate);
            }

            /* Third parameter can be used to pass settings to encoder */
            ret = avcodec_open2(enc_ctx, encoder, NULL);
            if (ret < 0) {
				av_log(NULL, AV_LOG_ERROR, "Error occurred: %s\n", av_err2str(ret));
                av_log(NULL, AV_LOG_ERROR, "Cannot open video encoder for stream #%u\n", i);
                return NULL;
            }
        } else if (dec_ctx->codec_type == AVMEDIA_TYPE_UNKNOWN) {
            av_log(NULL, AV_LOG_FATAL, "Elementary stream #%d is of unknown type, cannot proceed\n", i);
            return NULL;
        } else {
            /* if this stream must be remuxed */
            ret = avcodec_copy_context(ofmt_ctx->streams[i]->codec,
                    ifmt_ctx->streams[i]->codec);
            if (ret < 0) {
                av_log(NULL, AV_LOG_ERROR, "Copying stream context failed\n");
                return NULL;
            }
        }

        if (ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
            enc_ctx->flags |= CODEC_FLAG_GLOBAL_HEADER;

    }
    av_dump_format(ofmt_ctx, 0, filename, 1);

    if (!(ofmt_ctx->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&ofmt_ctx->pb, filename, AVIO_FLAG_WRITE);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Could not open output file '%s'", filename);
            return NULL;
        }
    }

    /* init muxer, write output file header */
    ret = avformat_write_header(ofmt_ctx, NULL);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Error occurred when opening output file\n");
        return NULL;
    }

    return ofmt_ctx;
}



/*
 * encode one video frame and send it to the muxer
 * return 1 when encoding is finished, 0 otherwise
 */
int write_video_frame(AVFormatContext *context, OutputStream *stream, AVFrame *frame)
{
    int ret;
    AVCodecContext *codec;
    
    int got_packet = 0;
    codec = stream->st->codec;
    if (context->oformat->flags & AVFMT_RAWPICTURE) {
        /* a hack to avoid data copy with some raw video muxers */
        AVPacket packet;
        av_init_packet(&packet);
        if (!frame)
            return 1;
        packet.flags        |= AV_PKT_FLAG_KEY;
        packet.stream_index  = stream->st->index;
        packet.data          = (uint8_t *)frame;
        packet.size          = sizeof(AVPicture);
        packet.pts = packet.dts = frame->pts;
        av_packet_rescale_ts(&packet, codec->time_base, stream->st->time_base);
        ret = av_interleaved_write_frame(context, &packet);
    } else {
        AVPacket packet = { 0 };
        av_init_packet(&packet);
        /* encode the image */
        ret = avcodec_encode_video2(codec, &packet, frame, &got_packet);
        if (ret < 0) {
            fprintf(stderr, "Error encoding video frame: %s\n", av_err2str(ret));
            exit(1);
        }
        if (got_packet) {
            ret = write_frame(context, &codec->time_base, stream->st, &packet);
        } else {
            ret = 0;
        }
    }
    if (ret < 0) {
        fprintf(stderr, "Error while writing video frame: %s\n", av_err2str(ret));
        exit(1);
    }
    return (frame || got_packet) ? 0 : 1;
}


void close_stream(AVFormatContext *context, OutputStream *stream)
{
    avcodec_close(stream->st->codec);
    av_frame_free(&stream->frame);
    av_frame_free(&stream->tmp_frame);
    sws_freeContext(stream->sws_ctx);
    swr_free(&stream->swr_ctx);
}


// 
const char * prepare(AVFormatContext **context, OutputStream *stream, const char *filename)
{
	int ret;
	//AVCodec *codec;
	AVDictionary *opt = NULL;
#ifdef _MSC_VER
	char drive[MAX_PATH], dir[MAX_PATH];
#else
	char buffer[MAX_PATH], *dir;
#endif
	static char temporary[MAX_PATH] = "temp.mp4";

    /* Initialize libavcodec, and register all codecs and formats. */
    av_register_all();

	if (0 <= getDuration(filename))
	{
		AVPacket packet = { 0 };
		OutputStream input_stream = {stream->width, stream->height, 0 };
		AVFormatContext *input_context = NULL;
		printf("DUPLICATE %s\n", filename);
#ifdef	_MSC_VER
		_splitpath(filename, drive, dir, NULL, NULL);	
		if (0 < strlen(drive))
		{
			sprintf(temporary, "%s:%s%s", drive, dir, "temp.mp4");
		}
		else
		{
			sprintf(temporary, "%s%s", dir, "temp.mp4");
		}
#else
		strcpy(buffer, filename);
		dir = dirname(buffer);
		sprintf(temporary, "%s/%s", dir, "temp.mp4");
#endif
		// duplicate
		input_context = open_input_file(filename);
		output(context, stream, temporary);
		// repeat packets	
		while (0 <= av_read_frame(input_context, &packet))
		{
			stream->last_pts = packet.pts;		// ここでインデックスを引き継ぐ
            av_packet_rescale_ts(&packet,
				input_context->streams[packet.stream_index]->time_base,
				(*context)->streams[packet.stream_index]->time_base);
            ret = av_interleaved_write_frame(*context, &packet);
		}
		stream->next_pts = (stream->last_pts * stream->st->codec->time_base.den * stream->st->time_base.num) 
			/ (stream->st->time_base.den * stream->st->codec->time_base.num);
		avformat_close_input(&input_context);
		return temporary;
	}
	else 
	{
		output(context, stream, filename);
		return filename;
	}
}

void finalize(AVFormatContext *context, OutputStream *stream)
{
	/* Write the trailer, if any. The trailer must be written before you
     * close the CodecContexts open when you wrote the header; otherwise
     * av_write_trailer() may try to use memory that was freed on
     * av_codec_close(). */
    av_write_trailer(context);

    /* Close each codec. */
    close_stream(context, stream);

    if (!(context->oformat->flags & AVFMT_NOFILE))
        /* Close the output file. */
        avio_closep(&context->pb);
    /* free the stream */
    avformat_free_context(context);
}
