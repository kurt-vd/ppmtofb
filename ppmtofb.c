#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <inttypes.h>

#include <unistd.h>
#include <fcntl.h>
#include <error.h>
#include <getopt.h>
#include <endian.h>
#include <byteswap.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <linux/fb.h>

#define NAME "ppmtofb"

/* ENDIANNESS */
#ifndef htobe32

static uint32_t _swap32(uint32_t v)
{
	uint8_t *ptr = (void *)&v;
	uint8_t tmp;

	tmp = ptr[3];
	ptr[3] = ptr[0];
	ptr[0] = tmp;

	tmp = ptr[2];
	ptr[2] = ptr[1];
	ptr[1] = tmp;
	return v;
}

static uint16_t _swap16(uint16_t v)
{
	uint8_t *ptr = (void *)&v;
	uint8_t tmp;

	tmp = ptr[1];
	ptr[1] = ptr[0];
	ptr[0] = tmp;
	return v;
}

#if BYTE_ORDER == LITTLE_ENDIAN

#define htobe32	_swap32
#define be32toh	_swap32
#define htobe16	_swap16
#define be16toh	_swap16
#define htole32(x)	(x)
#define le32toh(x)	(x)
#define htole16(x)	(x)
#define le16toh(x)	(x)

#else

#define htobe32(x)	(x)
#define be32toh(x)	(x)
#define htobe16(x)	(x)
#define be16toh(x)	(x)
#define htole32	_swap32
#define le32toh	_swap32
#define htole16	_swap16
#define le16toh	_swap16

#endif
#endif
/* ARGUMENTS */
static const char help_msg[] =
	NAME ": Convert between PPM and framebuffer (raw)\n"
	"Usage	: " NAME " [PPM [FBDEVICE]]\n"
	"	: " NAME " [FBDEVICE [PPM]]\n"
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

static int verbose = 0;

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

/* interal pixel representation */
static inline uint32_t mkpixel(int r, int g, int b, int a)
{
	return ((r & 0xff) << 16) | (( g & 0xff) << 8) | ((b & 0xff) << 0) | ((a & 0xff) << 24);
}

static inline uint8_t pixel_r(uint32_t pixel)
{
	return (pixel >> 16) & 0xff;
}

static inline uint8_t pixel_g(uint32_t pixel)
{
	return (pixel >> 8) & 0xff;
}

static inline uint8_t pixel_b(uint32_t pixel)
{
	return (pixel >> 0) & 0xff;
}

static inline uint8_t pixel_a(uint32_t pixel)
{
	return (pixel >> 24) & 0xff;
}

/* FB info */
struct fb_fix_screeninfo fix_info;
struct fb_var_screeninfo var_info;
uint16_t colormap_data[4][1 << 8];
struct fb_cmap colormap = {
	0,
	1 << 8,
	colormap_data[0],
	colormap_data[1],
	colormap_data[2],
	colormap_data[3],
};
static uint8_t *video;
/* cached framebuffer bytes per pixel */
static int fbbypp;

/* FRAMEBUFFER CONFIG */
static int getfbinfo(int fd)
{
	unsigned int i;
	struct stat st;

	if (fstat(fd, &st) < 0)
		error(1, errno, "fstat %i", fd);
	if (!S_ISCHR(st.st_mode))
		return -1;

	if (ioctl(fd, FBIOGET_FSCREENINFO, &fix_info)) {
		if (errno == ENOTTY)
			return -1;
		error(1, errno, "FBIOGET_FSCREENINFO failed");
	}

	if (fix_info.type != FB_TYPE_PACKED_PIXELS)
		error(1, 0, "framebuffer type is not PACKED_PIXELS (%i)", fix_info.type);

	if (ioctl(fd, FBIOGET_VSCREENINFO, &var_info))
		error(1, errno, "FBIOGET_VSCREENINFO failed");

	if (var_info.red.length > 8 || var_info.green.length > 8 || var_info.blue.length > 8)
		error(1, 0, "color depth > 8 bits per component");

	switch (fix_info.visual) {
	case FB_VISUAL_TRUECOLOR:
		/* initialize dummy colormap */
		for (i = 0; i < (1 << var_info.red.length); i++)
			colormap.red[i] = i * 0xffff / ((1 << var_info.red.length) - 1);
		for (i = 0; i < (1 << var_info.green.length); i++)
			colormap.green[i] = i * 0xffff / ((1 << var_info.green.length) - 1);
		for (i = 0; i < (1 << var_info.blue.length); i++)
			colormap.blue[i] = i * 0xffff / ((1 << var_info.blue.length) - 1);
		break;
	case FB_VISUAL_DIRECTCOLOR:
	case FB_VISUAL_PSEUDOCOLOR:
	case FB_VISUAL_STATIC_PSEUDOCOLOR:
		if (ioctl(fd, FBIOGETCMAP, &colormap) != 0)
			error(1, errno, "FBIOGETCMAP failed");
		break;
	default:
		error(1, 0, "unsupported visual (%i)", fix_info.visual);
	}
	fbbypp = (var_info.bits_per_pixel +7) /8;

	if (verbose) {
		error(0, 0, "framebuffer (%s) on %s", fix_info.id, fd ? "stdout" : "stdin");
		error(0, 0, "%ux%u, bytes/pixel %i", var_info.xres, var_info.yres, fbbypp);
		error(0, 0, "r %u/%u, g %u/%u, b %u/%u, a %u/%u",
				var_info.red.length, var_info.red.offset,
				var_info.green.length, var_info.green.offset,
				var_info.blue.length, var_info.blue.offset,
				var_info.transp.length, var_info.transp.offset);
	}
	return 0;
}

static uint8_t *getvideomemory(int fd, int wr)
{
	size_t len, offset;
	uint8_t *mem;
	

	offset = fix_info.line_length * var_info.yoffset;
	len = fix_info.line_length * var_info.yres;
	if (verbose)
		error(0, 0, "mapping video memory +%uKB", len/1024); 
	mem = mmap(NULL, len, wr ? PROT_WRITE : PROT_READ, MAP_SHARED, fd, offset);

	if (mem == MAP_FAILED)
		error(1, errno, "mmap failed");
	video = mem;
	return mem;
}

static void putvideomemory(void)
{
	munmap(video, fix_info.line_length*var_info.yres);
}

/* FRAMEBUFFER */
static inline uint8_t *getfbpos(int x, int y)
{
	return video + (y+var_info.yoffset)*fix_info.line_length + (x+var_info.xoffset)*fbbypp;
}

static inline uint8_t getfbcolor(uint32_t pixel, const struct fb_bitfield *bitfield, const uint16_t *colormap)
{
	return ((pixel >> bitfield->offset) & ((1 << bitfield->length)-1)) << (8 - bitfield->length);
	return colormap[(pixel >> bitfield->offset) & ((1 << bitfield->length) - 1)] >> 8;
}

static inline uint32_t putfbcolor(uint32_t color, const struct fb_bitfield *bitfield, const uint16_t *colormap)
{
	color >>= (8 - bitfield->length);
	return color << bitfield->offset;
}

static uint32_t getfbpixel(int x, int y)
{
	void *dat = getfbpos(x, y);
	uint32_t pixel = 0;

	switch (fbbypp) {
	case 1:
		pixel = *(uint8_t *)dat;
		break;
	case 2:
		pixel = le16toh(*(uint16_t *)dat);
		break;
	case 3:
		pixel = le32toh(*(uint32_t *)dat) & 0xffffff;
		break;
	case 4:
		pixel = le32toh(*(uint32_t *)dat);
		break;
	}
	return mkpixel(getfbcolor(pixel, &var_info.red, colormap.red),
			getfbcolor(pixel, &var_info.green, colormap.green),
			getfbcolor(pixel, &var_info.blue, colormap.blue),
			getfbcolor(pixel, &var_info.transp, colormap.transp));
}

static void putfbpixel(int x, int y, uint32_t pixel)
{
	void *dat = getfbpos(x, y);
	uint32_t fbpixel;

	fbpixel = putfbcolor(pixel_r(pixel), &var_info.red, colormap.red)
		| putfbcolor(pixel_g(pixel), &var_info.green, colormap.green)
		| putfbcolor(pixel_b(pixel), &var_info.blue, colormap.blue)
		| putfbcolor(pixel_a(pixel), &var_info.transp, colormap.transp);
	/* assume framebuffer is in Little Endian */

	switch (fbbypp) {
	case 1:
		*(uint8_t *)dat = fbpixel;
		break;
	case 2:
		*(uint16_t *)dat = htole16(fbpixel);
		break;
	case 3:
	case 4:
		*(uint32_t *)dat = htole32(fbpixel);
		break;
	}
}

/* PPM */
static uint32_t getppmpixel(const uint8_t *dat, int max)
{
	if (max == 0xff)
		return mkpixel(dat[0], dat[1], dat[2], 0);
	else if (max == 0xffff)
		return mkpixel(dat[0], dat[2], dat[4], 0);
	else if (max > 255) {
		const uint16_t *dat16 = (const void *)dat;

		return mkpixel(be16toh(dat16[0])*255/max,
				be16toh(dat16[1])*255/max,
				be16toh(dat16[2])*255/max,
				0);
	} else
		return mkpixel(dat[0]*255/max, dat[1]*255/max,
				dat[2]*255/max, 0);
}

static void putppmpixel(uint32_t pixel)
{
	pixel = htobe32(pixel);

	if (fwrite(&pixel, 3/* only 3 bytes! */, 1, stdout) < 0)
		error(1, errno, "writing pixels");
}

int main (int argc, char *argv[])
{
	int opt, size, max;
	int w, h, r, c;

	/* argument parsing */
	while ((opt = getopt_long(argc, argv, optstring, long_opts, NULL)) != -1)
	switch (opt) {
	case 'V':
		fprintf(stderr, "%s %s\n", NAME, VERSION);
		return 0;
	case 'v':
		++verbose;
		break;
	default:
		fputs(help_msg, stderr);
		exit(1);
		break;
	}

	/* redir stdout/stdin */
	if (argv[optind] && strcmp(argv[optind], "-")) {
		int fd;

		fd = open(argv[optind], O_RDONLY);
		if (fd < 0)
			error(1, errno, "open %s", argv[optind]);
		dup2(fd, STDIN_FILENO);
		close(fd);
	}
	if (argv[optind] && argv[optind+1] && strcmp(argv[optind+1], "-")) {
		int fd;

		fd = open(argv[optind+1], O_RDWR | O_CREAT | O_TRUNC, 0666);
		if (fd < 0)
			error(1, errno, "open %s", argv[optind+1]);
		dup2(fd, STDOUT_FILENO);
		close(fd);
	}

	if (getfbinfo(STDOUT_FILENO) == 0) {
		/* copy ppm to fb */
		char *str;
		int imgw, ppmbypp;
		uint8_t *dat = NULL, *d8;

		if (verbose)
			error(0, 0, "PPM -> FB");

		size = readallocfile(&dat, "-");
		if (size <= 0)
			error(1, errno, "read file %s", argv[optind] ?: "-");

		str = (void *)dat;
		if (strncmp(str, "P6", 2))
			error(1, errno, "no PPM file");
		imgw = w = strtoul(str+2, &str, 0);
		h = strtoul(str, &str, 0);
		max = strtoul(str, &str, 0);
		++str;

		if (h > var_info.yres)
			h = var_info.yres;
		if (w > var_info.xres)
			w = var_info.xres;
        
		getvideomemory(STDOUT_FILENO, 1);

		d8 = (uint8_t *)str;
		ppmbypp = (max > 255) ? 6 : 3;
		for (r = 0; r < h; ++r) {
			for (c = 0; c < w; ++c, d8 += ppmbypp)
				putfbpixel(c, r, getppmpixel(d8, max));
			if (imgw > var_info.xres)
				d8 += (imgw - var_info.xres)*ppmbypp;
		}
		putvideomemory();
		free(dat);
	} else if (getfbinfo(STDIN_FILENO) == 0) {
		/* copy fb to ppm */
		if (verbose)
			error(0, 0, "FB -> PPM");
		getvideomemory(STDIN_FILENO, 0);
		w = var_info.xres;
		h = var_info.yres;

		if (!w || !h)
			error(1, 0, "width & height must be != 0");

		/* PPM header */
		printf("P6 %u %u 255\n", w, h);

		for (r = 0; r < h; ++r) {
			for (c = 0; c < w; ++c)
				putppmpixel(getfbpixel(c, r));
		}
		putvideomemory();
	} else {
		error(1, errno, "no framebuffer on stdin or stdout?");
	}
	fflush(stdout);
	return 0;
}

