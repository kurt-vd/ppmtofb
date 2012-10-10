#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <inttypes.h>

#include <unistd.h>
#include <fcntl.h>
#include <error.h>
#include <getopt.h>
#include <sys/stat.h>

#define NAME "ppmtofb"

/* ARGUMENTS */
static const char help_msg[] =
	NAME ": Convert PPM to framebuffer (16bit) raw files\n"
	"Usage: " NAME " [INPUT]\n"
	;

#ifdef _GNU_SOURCE
static const struct option long_opts[] = {
	{ "help", no_argument, NULL, '?', },
	{ "version", no_argument, NULL, 'V', },
	{ },
};

#else
#define getopt_long(argc, argv, optstring, longopts, longindex) \
	getopt((argc), (argv), (optstring))
#endif

static const char optstring[] = "?Vv";

int readallocfile(uint8_t **dat, const char *filename)
{
	int ret, fd, done;
	struct stat st;

	if (!filename || !strcmp("-", filename)) {
		fd = fileno(stdin);
	} else {
		fd = open(filename, O_RDONLY);
		if (fd < 0)
			error(1, errno, "open %s", filename);
	}

	fstat(fd, &st);
	if (st.st_size) {
		done = st.st_size;
		*dat = realloc(*dat, done);
		if (!*dat)
			error(1, errno, "realloc %u", done);
		ret = read(fd, *dat, done);
		if (ret < 0)
			error(1, errno, "read %s", filename);
	} else {
#define BLK	1024
		int size;

		done = size = 0;
		do {
			size += BLK;
			*dat = realloc(*dat, size);
			ret = read(fd, (*dat)+done, BLK);
			if (ret < 0)
				error(1, errno, "read %s", filename);
			done += ret;
		} while (ret == BLK);
		ret = done;
	}
	return ret;
}

int main (int argc, char *argv[])
{
	int opt, size, off, max;
	char *str;
	uint8_t *dat = NULL, *d8;
	uint16_t u16;
	int w, h, r, c;

	/* argument parsing */
	while ((opt = getopt_long(argc, argv, optstring, long_opts, NULL)) != -1)
	switch (opt) {
	case 'V':
		fprintf(stderr, "%s %s\n", NAME, VERSION);
		return 0;
	default:
		fputs(help_msg, stderr);
		exit(1);
		break;
	}

	size = readallocfile(&dat, argv[optind] ?: "-");
	if (size <= 0)
		error(1, errno, "read file %s", argv[optind]);

	str = (void *)dat;
	if (!strncmp(str, "P6", 2))
		error(1, errno, "no PPM file");
	w = strtoul(str+2, &str, 0);
	h = strtoul(str, &str, 0);
	max = strtoul(str, &str, 0);
	++str;
	off = str - (char *)dat;

	if (max >= 255)
		error(1, 0, "maxval(%u) > 255", max);

	d8 = (uint8_t *)str;
	for (r = 0; r < h; ++r) {
		for (c = 0; c < w; ++c, d8 +=3) {
			u16 = ((d8[0] & 0xf8) << 8) |
				((d8[1] & 0xfc) << 2) |
				((d8[2] & 0xf8) >> 3);
			fwrite(&u16, sizeof(u16), 1, stdout);
		}
	}
	fflush(stdout);
	return 0;
}

