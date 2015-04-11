//
//
#include <error.h>
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


#ifndef	_MSC_VER
int selector(const struct dirent *dir)
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
	int ret;
    OutputStream stream = {WIDTH, HEIGHT, 0 };
    const char *filename, *pictures, *temporary;
    AVFormatContext *context = NULL;	
	char drive[MAX_PATH], dir[MAX_PATH];

	int sortByName = 1;
	int c;
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

	if (argc < 3) {
		Usage(argv[0]);
        return 1;
    }

    filename = argv[1];
	pictures = argv[2];

	// parse options
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

	/////////////////////////////////////////
	// prepare for output
	temporary = prepare(&context, &stream, filename);

	////////////////////////////////////////////////////////////
	// append pictures
#ifdef	_MSC_VER	
	mbstowcs_s(&wlen, buffer, MAX_PATH, pictures, MAX_PATH);
	_splitpath(pictures, drive, dir, NULL, NULL);	
	if (0 < strlen(drive))
	{
		sprintf(input_path, "%s:%s", drive, dir);
	}
	else
	{
		sprintf(input_path, "%s", dir);
	}
	if ((hFind = FindFirstFile(buffer, &ffd)) == INVALID_HANDLE_VALUE) {
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
		numPics = scandir(pictures, &namelist, selector, alphasort);
	}
	else 
	{
		numPics = scandir(pictures, &namelist, selector, timesort);
	}
	for (i = 0; i < numPics; ++i) {
		AVFrame *frame;
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

	////////////////////////////////////////////////////////////
	// finish
	finalize(context, &stream);
	if (strcmp(filename, temporary) != 0)
	{
		ret = remove(filename);
		ret = rename(temporary, filename);
		if (ret != 0)
		{
			printf("%d\n", errno);
		}
	}

    return 0;
}
