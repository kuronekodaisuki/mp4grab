//
//
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
	int sortByName = 1;
	int c;
	int encode_video = 0;
	int dup = 0;

#ifdef	_MSC_VER
	HANDLE hFind = INVALID_HANDLE_VALUE;
	WIN32_FIND_DATA ffd;
	char input_path[MAX_PATH] = "..\\Archive\\";
	WCHAR buffer[MAX_PATH] = L"..\\Archive\\*.png";
	size_t wlen;
	char drive[MAX_PATH], dir[MAX_PATH];
#else
	char buffer[MAX_PATH], dir[MAX_PATH], *input_path;
	DIR *directory;
	struct dirent *entry, **namelist;
	int i, numPics = 0;
#endif

	if (argc < 3) {
		Usage(argv[0]);
        return 1;
    }

    filename = argv[1];
	pictures = argv[2];

	// parse options
	while ((c = getopt(argc, argv, "w:h:td")) != -1) {
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

		case 'd':	// dup and exit
			dup = 1;
			break;
		}
	}

	/////////////////////////////////////////
	// prepare for output
	temporary = prepare(&context, &stream, filename);
	if (dup) {
		finalize(context, &stream);
		exit(0);
	}
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
	if ((hFind = FindFirstFile(buffer, &ffd)) != INVALID_HANDLE_VALUE) {
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
	strcpy(dir, pictures);
	/*
	directory = opendir(pictures);
	for (entry = readdir(directory); entry != NULL; entry = readdir(directory)) {
		if (entry->d_type != DT_DIR) {
			printf("Read: %s\n", entry->d_name);
		}
	}
	closedir(directory);
	*/
	//input_path = dirname(dir);
	if (sortByName)
	{
		numPics = scandir(pictures, &namelist, selector, alphasort);
	}
	else 
	{
		numPics = scandir(pictures, &namelist, selector, timesort);
	}
	printf("%d pictures\n", numPics);
	for (i = 0; i < numPics; ++i) {
		AVFrame *frame;
		sprintf(buffer, "%s/%s", dir, namelist[i]->d_name);
//#ifdef	_DEBUG
		printf("Read: %s\n", buffer);
//#endif
		frame = read_image_frame(&stream, buffer);
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
