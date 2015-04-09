//
//
#ifdef	_MSC_VER
#include <windows.h>
#include <stdint.h>
#pragma warning(disable: 4996)
#include "getopt.h"
#else
#include <dirent.h>
#include <getopt.h>
#endif
#include "mp4grab.h"

void Usage(const char *app)
{
    printf("USAGE: %s output_file picture_file [FPS [-h ddd -w ddd][-t]]\n"
		"\toutput_file: Path to video file(Relative/absolute)\n"
		"\tpicture_file: Path to picture file(Relative/absolute)\n"
		"\tFPS: frame per second\n"
		"\t-h: option for height specified ddd\n"
		"\t-w: option for width specified ddd\n"
		"\t-t: option for sort by time stamp(default alphabetic name order\n"
		"\n", app);
}

// get duration of movie file
int64_t getDuration(const char *filename)
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

#ifndef	_MSC_VER
int selector(struct dirent *dir)
{
	if(dir->d_name[0] == '.')
	{
		return (0);
	}
	return (1);
}

int timesort(const struct dirent **s1, const struct dirent **s2)
{
	return strcmp( (*s1)->d_name, (*s2)->d_name);	// TODO fix by timestamp
}
#endif

int main(int argc, char *argv[])
{
    OutputStream stream = {WIDTH, HEIGHT, 0 };
    const char *filename;
    AVOutputFormat *fmt;
    AVFormatContext *context;
    AVCodec *video_codec;
    AVDictionary *opt = NULL;
	int64_t	duration = -1;

	int sortByName = 1;
	int c;
	int ret;
    int have_video = 0;
    int encode_video = 0;

#ifdef	_MSC_VER
	HANDLE hFind = INVALID_HANDLE_VALUE;
	WIN32_FIND_DATA ffd;
	char input_path[MAX_PATH] = "..\\Archive\\";
	WCHAR buffer[MAX_PATH] = L"..\\Archive\\*.png";
	size_t wlen;
#else
	struct dirent **namelist;
	int i, numPics = 0;
#endif

    /* Initialize libavcodec, and register all codecs and formats. */
    av_register_all();

	if (argc < 2) {
		Usage(argv[0]);
        return 1;
    }

    filename = argv[1];
	duration = getDuration(filename);

    if (3 <= argc) {
#ifdef	_MSC_VER	
		char drive[MAX_PATH], dir[MAX_PATH];
		mbstowcs_s(&wlen, buffer, MAX_PATH, argv[2], MAX_PATH);
		_splitpath(argv[2], drive, dir, NULL, NULL);	
		if (0 < strlen(drive))
		{
			sprintf(input_path, "%s:%s", drive, dir);
		}
		else
		{
			sprintf(input_path, "%s", dir);
		}
		hFind = FindFirstFile(buffer, &ffd);
#endif
    }

	while ((c = getopt(argc, argv, "w:h:t")) != -1) {
		int value = -1;
		switch (c)
		{
		case 'w':	// width of movie resolution
			value = atoi(optarg);
			if (0 < value)
			{
				stream.width = value;
			}
			printf("width: %s\n", optarg);
			break;

		case 'h':	// height of movie resolution
			value = atoi(optarg);
			if (0 < value)
			{
				stream.height = value;
			}
			printf("height: %s\n", optarg);
			break;

		case 't':	// sort by time stamp
			sortByName = 0;
			break;
		}
	}

    /* allocate the output media context */
    avformat_alloc_output_context2(&context, NULL, NULL, filename);
    if (!context) {
        printf("Could not deduce output format from file extension: using MPEG.\n");
        avformat_alloc_output_context2(&context, NULL, "mpeg", filename);
    }
    if (!context)
        return 1;
    fmt = context->oformat;
    
	/* Add the audio and video streams using the default format codecs
     * and initialize the codecs. */
    if (fmt->video_codec != AV_CODEC_ID_NONE) {
        add_stream(context, &stream, &video_codec, fmt->video_codec);
        have_video = 1;
        encode_video = 1;
    }

    /* Now that all the parameters are set, we can open the audio and
     * video codecs and allocate the necessary encode buffers. */
    if (have_video)
        open_video(context, &stream, video_codec, opt);

    av_dump_format(context, 0, filename, 1);

    /* open the output file, if needed */
    if (!(fmt->flags & AVFMT_NOFILE)) {
        ret = avio_open(&context->pb, filename, AVIO_FLAG_READ_WRITE);
        if (ret < 0) {
            fprintf(stderr, "Could not open '%s': %s\n", filename,
                    av_err2str(ret));
            return 1;
        }
    }

	/* Write the stream header, if any. */
	ret = avformat_write_header(context, &opt);
	if (ret < 0) {
		fprintf(stderr, "Error occurred when opening output file: %s\n",
				av_err2str(ret));
		return 1;
	}
	/*
	if (0 < duration) {
		int64_t tm = duration;
		int stream_index = av_find_default_stream_index(context);
        //Convert ts to frame
        tm = av_rescale(tm, STREAM_FRAME_RATE, 1);
        tm /= 1000;

        //SEEK
        if (avformat_seek_file(context, stream_index, 0, tm, INT64_MAX, AVSEEK_FLAG_FRAME) < 0) {
		//ret = avformat_seek_file(context, 0, duration, AVSEEK_FLAG_FRAME);
		//if (ret < 0) {
			fprintf(stderr, "Error occurred when opening output file: %s\n",
					av_err2str(ret));
			return 1;
		}
	}
	*/

#ifdef	_MSC_VER	
		if (hFind == INVALID_HANDLE_VALUE) {
			AVFrame *frame;
			while (encode_video) {
				frame = get_dummy_frame(&stream);
				encode_video = !write_video_frame(context, &stream, frame);
			}
		}
		else {
			AVFrame *frame;
			do {
				if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
				{
				}
				else 
				{
					char name[80], buffer[100];
					size_t len;

					wcstombs_s(&len, name, 80, ffd.cFileName, 80);
					sprintf(buffer, "%s%s", input_path, name);
#ifdef	_DEBUG
					printf("Read: %s\n", buffer);
#endif
					frame = read_image_frame(&stream, buffer);
					if (write_video_frame(context, &stream, frame))
					{
						break;
					}
				}
			} while (FindNextFile(hFind, &ffd) != 0);
		}
#else	// linux
		if (sortByName)
		{
			numPics = scandir(dirname, &namelist, selector, alphasort);
		}
		else 
		{
			numPics = scandir(dirname, &namelist, selector, timesort);
		}
		for (i = 0; i < numPics; ++i) {
#ifdef	_DEBUG
			printf("Read: %s\n", namelist[i]->d_name);
#endif
			frame = read_image_frame(&stream, namelist[i]->d_name);
			if (write_video_frame(context, &stream, frame))
			{
				break;
			}
			free(namelist[i]);
		}
		free(namelist);
#endif

    /* Write the trailer, if any. The trailer must be written before you
     * close the CodecContexts open when you wrote the header; otherwise
     * av_write_trailer() may try to use memory that was freed on
     * av_codec_close(). */
    av_write_trailer(context);

    /* Close each codec. */
    if (have_video)
        close_stream(context, &stream);
    //if (have_audio)
    //    close_stream(context, &audio_st);

    if (!(fmt->flags & AVFMT_NOFILE))
        /* Close the output file. */
        avio_closep(&context->pb);
    /* free the stream */
    avformat_free_context(context);
    return 0;
}
