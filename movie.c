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
#endif

static int write_frame(AVFormatContext *fmt_ctx, const AVRational *time_base, AVStream *st, AVPacket *pkt)
{
    /* rescale output packet timestamp values from codec to stream timebase */
    av_packet_rescale_ts(pkt, *time_base, st->time_base);
    pkt->stream_index = st->index;
    /* Write the compressed frame to the media file. */
    //log_packet(fmt_ctx, pkt);
    return av_interleaved_write_frame(fmt_ctx, pkt);
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

/* Prepare a dummy image. */
static void fill_yuv_image(AVFrame *pict, int frame_index,
                           int width, int height)
{
    int x, y, i, ret;
    /* when we pass a frame to the encoder, it may keep a reference to it
     * internally;
     * make sure we do not overwrite it here
     */
    ret = av_frame_make_writable(pict);
    if (ret < 0)
        exit(1);
    i = frame_index;
    /* Y */
    for (y = 0; y < height; y++)
        for (x = 0; x < width; x++)
            pict->data[0][y * pict->linesize[0] + x] = x + y + i * 3;
    /* Cb and Cr */
    for (y = 0; y < height / 2; y++) {
        for (x = 0; x < width / 2; x++) {
            pict->data[1][y * pict->linesize[1] + x] = 128 + y + i * 2;
            pict->data[2][y * pict->linesize[2] + x] = 64 + x + i * 5;
        }
    }
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

AVFrame *get_dummy_frame(OutputStream *stream)
{
    AVCodecContext *c = stream->st->codec;
    /* check if we want to generate more frames */
    if (av_compare_ts(stream->next_pts, stream->st->codec->time_base,
		(int64_t)STREAM_DURATION, av_create_rational( 1, 1 )) >= 0)
        return NULL;
    if (c->pix_fmt != AV_PIX_FMT_YUV420P) {
        /* as we only generate a YUV420P picture, we must convert it
         * to the codec pixel format if needed */
        if (!stream->sws_ctx) {
            stream->sws_ctx = sws_getContext(c->width, c->height,
                                          AV_PIX_FMT_YUV420P,
                                          c->width, c->height,
                                          c->pix_fmt,
                                          SCALE_FLAGS, NULL, NULL, NULL);
            if (!stream->sws_ctx) {
                fprintf(stderr,
                        "Could not initialize the conversion context\n");
                exit(1);
            }
        }
        fill_yuv_image(stream->tmp_frame, (int)stream->next_pts, c->width, c->height);
        sws_scale(stream->sws_ctx,
                  (const uint8_t * const *)stream->tmp_frame->data, stream->tmp_frame->linesize,
                  0, c->height, stream->frame->data, stream->frame->linesize);
    } else {
        fill_yuv_image(stream->frame, (int)stream->next_pts, c->width, c->height);
    }
    stream->frame->pts = stream->next_pts++;
    return stream->frame;
}

/////////////////////////////////////////////////////////////////////////////
/* Add an output stream. */
void add_stream(AVFormatContext *context, OutputStream *stream, 
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

void open_video(AVFormatContext *context, OutputStream *stream, AVCodec *codec, AVDictionary *opt_arg)
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

/*
 * encode one video frame and send it to the muxer
 * return 1 when encoding is finished, 0 otherwise
 */
int write_video_frame(AVFormatContext *context, OutputStream *stream, AVFrame *frame)
{
    int ret;
    AVCodecContext *c;
    
    int got_packet = 0;
    c = stream->st->codec;
    if (context->oformat->flags & AVFMT_RAWPICTURE) {
        /* a hack to avoid data copy with some raw video muxers */
        AVPacket pkt;
        av_init_packet(&pkt);
        if (!frame)
            return 1;
        pkt.flags        |= AV_PKT_FLAG_KEY;
        pkt.stream_index  = stream->st->index;
        pkt.data          = (uint8_t *)frame;
        pkt.size          = sizeof(AVPicture);
        pkt.pts = pkt.dts = frame->pts;
        av_packet_rescale_ts(&pkt, c->time_base, stream->st->time_base);
        ret = av_interleaved_write_frame(context, &pkt);
    } else {
        AVPacket pkt = { 0 };
        av_init_packet(&pkt);
        /* encode the image */
        ret = avcodec_encode_video2(c, &pkt, frame, &got_packet);
        if (ret < 0) {
            fprintf(stderr, "Error encoding video frame: %s\n", av_err2str(ret));
            exit(1);
        }
        if (got_packet) {
            ret = write_frame(context, &c->time_base, stream->st, &pkt);
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