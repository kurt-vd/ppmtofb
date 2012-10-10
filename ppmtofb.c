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
	NAME ": Convert PPM to framebuffer (raw)\n"
	"Usage: " NAME " [INPUT]\n"
	"\n"
	"Options:\n"
	" -t, --type=TYPE	Use VAL bits per pixel (rgb565*, rgb, rgba)\n"
	;

#ifdef _GNU_SOURCE
static const struct option long_opts[] = {
	{ "help", no_argument, NULL, '?', },
	{ "version", no_argument, NULL, 'V', },

	{ "type", required_argument, NULL, 't', },
	{ },
};

#else
#define getopt_long(argc, argv, optstring, longopts, longindex) \
	getopt((argc), (argv), (optstring))
#endif

static const char optstring[] = "?Vvt:";

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

static int putrgb565(int r, int g, int b)
{
	uint16_t val;

	val = ((r & 0xf8) << 8) |
		((g & 0xfc) << 2) |
		((b & 0xf8) >> 3);
	return fwrite(&val, sizeof(val), 1, stdout);
}

static int putrgb(int r, int g, int b)
{
	uint8_t val[3] = { r, g, b, };

	return fwrite(&val, sizeof(val), 1, stdout);
}

static int putrgba(int r, int g, int b)
{
	uint8_t val[4] = { r, g, b, 0, };

	return fwrite(&val, sizeof(val), 1, stdout);
}

struct puttype {
	const char *name;
	int (*fn)(int r, int g, int b);
};

static const struct puttype puttypes[] = {
	{ "rgb565", putrgb565, },
	{ "rgb", putrgb, },
	{ "rgba", putrgba, },
	{},
};

int main (int argc, char *argv[])
{
	int opt, size, off, max, j;
	char *str;
	uint8_t *dat = NULL, *d8;
	int w, h, r, c;
	int (*put)(int r, int g, int b) = puttypes[0].fn;

	/* argument parsing */
	while ((opt = getopt_long(argc, argv, optstring, long_opts, NULL)) != -1)
	switch (opt) {
	case 'V':
		fprintf(stderr, "%s %s\n", NAME, VERSION);
		return 0;
	case 't':
		for (j = 0; puttypes[j].name; ++j) {
			if (!strcasecmp(puttypes[j].name, optarg)) {
				put = puttypes[j].fn;
				goto type_found;
			}
		}
		error(1, 0, "bpp '%s' not supported", optarg);
		type_found:
		break;
	default:
		fputs(help_msg, stderr);
		exit(1);
		break;
	}

	size = readallocfile(&dat, argv[optind] ?: "-");
	if (size <= 0)
		error(1, errno, "read file %s", argv[optind]);

	str = (void *)dat;
	if (strncmp(str, "P6", 2))
		error(1, errno, "no PPM file");
	w = strtoul(str+2, &str, 0);
	h = strtoul(str, &str, 0);
	max = strtoul(str, &str, 0);
	++str;
	off = str - (char *)dat;

	if (max > 255)
		error(1, 0, "maxval(%u) > 255", max);

	d8 = (uint8_t *)str;
	for (r = 0; r < h; ++r) {
		for (c = 0; c < w; ++c, d8 +=3)
			put(d8[0], d8[1], d8[2]);
	}
	fflush(stdout);
	return 0;
}

