#include <stdio.h>
#include <png.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <getopt.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <assert.h>

#define PIXBYTE(bitdepth) (3*((bitdepth-1)/8+1))

struct pngimage {
	png_uint_32 height, width;
	int bitdepth, colortyp;
	unsigned char *area;
	png_bytep *rows;
	int alloc;
};

static struct pngimage pim;

static void onerow_done(png_structp png, png_uint_32 row, int pass)
{
	if (pim.height == 0)
		return;
	printf("\b\b\b");
	printf("%02d%%", (100*row/pim.height));
	fflush(stdout);
}

static int png_read(FILE *fi, struct pngimage *im)
{
	int retv = 0;
	png_structp png;
	png_infop pnginfo, endinfo;
	int interlace_type, comp_type, filter_method;
	unsigned char *draw, *cline;
	unsigned char **c_row;
	int i, pixbytes;

	im->alloc = 0;
	png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	if (!png) {
		fprintf(stderr, "Cannot allocate PNG Structure, Out of Memory!\n");
		return 10000;
	}
	png_set_read_status_fn(png, onerow_done);
	png_init_io(png, fi);

	pnginfo = png_create_info_struct(png);
	if (!pnginfo) {
		fprintf(stderr, "Cannot allocate PNG Info, Out of Memory!\n");
		retv = 10000;
		goto exit_10;
	}
	endinfo = png_create_info_struct(png);
	if (!endinfo) {
		fprintf(stderr, "Cannot allocate PNG Info, Out of Memory!\n");
		retv = 10000;
		endinfo = NULL;
		goto exit_12;
	}
	png_read_info(png, pnginfo);
	png_get_IHDR(png, pnginfo, &im->width, &im->height, &im->bitdepth,
		&im->colortyp, &interlace_type, &comp_type, &filter_method);

	if ((im->colortyp & ~PNG_COLOR_MASK_ALPHA) == PNG_COLOR_TYPE_GRAY)
		png_set_gray_to_rgb(png);
	if (im->colortyp == PNG_COLOR_TYPE_PALETTE)
		png_set_palette_to_rgb(png);
	if (im->colortyp & PNG_COLOR_MASK_ALPHA)
		png_set_strip_alpha(png);
	png_read_update_info(png, pnginfo);
	png_get_IHDR(png, pnginfo, &im->width, &im->height, &im->bitdepth,
		&im->colortyp, &interlace_type, &comp_type, &filter_method);

	printf("Pixcel bit depth: %d\n", im->bitdepth);
	pixbytes = PIXBYTE(im->bitdepth);
	draw = mmap(NULL, im->width*im->height*pixbytes, PROT_READ|PROT_WRITE,
		MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
	if (!draw) {
		fprintf(stderr, "Out of Memory!\n");
		retv = 10000;
		goto exit_16;
	}
	im->area = draw;
	im->rows = malloc(im->height*sizeof(png_bytep *));
	if (!im->rows) {
		fprintf(stderr, "Out of Memory!\n");
		retv = 1000;
		goto exit_18;
	}
	im->alloc = 1;
	cline = draw;
	for (c_row = im->rows, i = 0; i < im->height; i++, c_row++) {
		*c_row = cline;
		cline += im->width*pixbytes;
	}
	
	printf("Reading Image:   ");
	png_read_image(png, im->rows);
	png_read_end(png, endinfo);
	printf("Done\n");

	png_destroy_read_struct(&png, &pnginfo, &endinfo);

	return retv;
	
exit_18:
	munmap(draw, (im->width*im->height*pixbytes));
exit_16:
	png_destroy_read_struct(&png, &pnginfo, &endinfo);
	return retv;

exit_12:
	png_destroy_read_struct(&png, &pnginfo, NULL);
	return retv;

exit_10:
	png_destroy_read_struct(&png, NULL, NULL);
	return retv;
}

static int png_write(FILE *fo, struct pngimage *im)
{
	int retv = 0;
	png_structp png;
	png_infop pnginfo;

	png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	if (!png) {
		fprintf(stderr, "Cannot allocate write png struct!\n");
		return 200;
	}
	pnginfo = png_create_info_struct(png);
	if (!pnginfo) {
		fprintf(stderr, "Cannot allocate write png info!\n");
		retv = 204;
		goto exit_10;
	}

	png_init_io(png, fo);
	png_set_IHDR(png, pnginfo, im->width, im->height, im->bitdepth,
		im->colortyp, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
		PNG_FILTER_TYPE_DEFAULT);
	png_set_filter(png, 0, PNG_FILTER_NONE);

	printf("Writing Image...   ");
	png_set_write_status_fn(png, onerow_done);
	png_write_info(png, pnginfo);
	png_write_image(png, im->rows);
	png_write_end(png, pnginfo);
	printf("Done!\n");

	png_destroy_write_struct(&png, &pnginfo);
	return retv;
exit_10:
	png_destroy_write_struct(&png, NULL);
	return retv;
}

static inline int min(unsigned int x, unsigned int y)
{
	if (x < y)
		return x;
	else
		return y;
}

struct cmdparam {
	const char *png_in, *png_out, *ssd;
	int cx, cy, width, height;
};

static int crop_image(struct cmdparam *crop, struct pngimage *im)
{
	int retv = 0;
	int pixbytes;
	unsigned char *area;
	png_bytep *rows;
	unsigned char **crow, *cline, *oline;
	int i;

	if (crop->cx + crop->width + 48 > im->width)
		return 100;
	if (crop->cy + crop->height + 32 > im->height)
		return 100;
	crop->width = min(im->width - crop->cx, crop->width);
	crop->height = min(im->height - crop->cy, crop->height);

	pixbytes = PIXBYTE(im->bitdepth);
	area = mmap(NULL, crop->width*crop->height*pixbytes,
		PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
	if (!area) {
		fprintf(stderr, "Out of Memory!\n");
		return 10000;
	}
	rows = malloc(crop->height*sizeof(png_bytep *));
	if (!rows) {
		fprintf(stderr, "Out of Memory!\n");
		retv = 10000;
		goto exit_10;
	}

	cline = area;
	oline = im->area + im->width*pixbytes*crop->cy;
	for (crow = rows, i = 0; i < crop->height; i++, crow++) {
		*crow = cline;
		memcpy(cline, oline+crop->cx*pixbytes, crop->width*pixbytes);
		cline += crop->width * pixbytes;
		oline += im->width * pixbytes;
	}
	munmap(im->area, im->width*im->height*pixbytes);
	free(im->rows);
	im->area = area;
	im->rows = rows;
	im->width = crop->width;
	im->height = crop->height;

	return retv;

exit_10:
	munmap(area, crop->width*crop->height*pixbytes);
	return retv;
}

static const struct option l_opts[] = {
	{
		.name = "cx",
		.has_arg = required_argument,
		.flag = NULL,
		.val = 'x'
	},
	{
		.name = "cy",
		.has_arg = required_argument,
		.flag = NULL,
		.val = 'y'
	},
	{
		.name = "width",
		.has_arg = required_argument,
		.flag = NULL,
		.val = 'w'
	},
	{
		.name = "height",
		.has_arg = required_argument,
		.flag = NULL,
		.val = 'h'
	},
	{
		.name = "png",
		.has_arg = required_argument,
		.flag = NULL,
		.val = 'p'
	},
	{
		.name = "out",
		.has_arg = required_argument,
		.flag = NULL,
		.val = 'o'
	},
	{
		.name = "ssd",
		.has_arg = required_argument,
		.flag = NULL,
		.val = 's'
	},
	{	.name = NULL	}
};

static int parse_cmdline(int argc, char *argv[], struct cmdparam *cmdarg)
{
	int opt, fin = 0, argpos, retv = 0;
	struct stat fs;
	extern int opterr, optopt;

	opterr = 0;
	do {
		opt = getopt_long(argc, argv, ":x:s:y:w:h:p:o:", l_opts, &argpos);
		switch(opt) {
		case -1:
			fin = 1;
			break;
		case 'x':
			cmdarg->cx = atoi(optarg);
			break;
		case 'y':
			cmdarg->cy = atoi(optarg);
			break;
		case 'w':
			cmdarg->width = atoi(optarg);
			break;
		case 'h':
			cmdarg->height = atoi(optarg);
			break;
		case 'p':
			cmdarg->png_in = optarg;
			break;
		case 'o':
			cmdarg->png_out = optarg;
			break;
		case 's':
			cmdarg->ssd = optarg;
			break;
		case '?':
			fprintf(stderr, "Unknown option: %c\n", optopt);
			retv = 110;
			break;
		case ':':
			fprintf(stderr, "Missing arguments: %c\n", optopt);
			retv = 114;
			break;
		default:
			fprintf(stderr, "Unprocessed options!\n");
			retv = 118;
			assert(0);
		}
	} while (fin != 1);
	if (!cmdarg->png_in || !cmdarg->png_out) {
		fprintf(stderr, "Input and/or Output file name missing!\n");
		return 112;
	}
	if (stat(cmdarg->png_in, &fs) == -1 ||
		!S_ISREG(fs.st_mode) || !(fs.st_mode & S_IRUSR)) {
		fprintf(stderr, "Invalied file: %s\n", cmdarg->png_in);
		return 104;
	}
	return retv;
}

static unsigned short rgb2ssd(const unsigned char *rgb)
{
	static const double rb_ratio = 31.0/255.0, g_ratio = 63.0/255.0;
	unsigned int r, g, b, rs, gs, bs;
	double rd, gd, bd;

	r = *rgb;
	g = *(rgb+1);
	b = *(rgb+2);

	rd = r*rb_ratio;
	gd = g*g_ratio;
	bd = b*rb_ratio;

	rs = rd;
	gs = gd;
	bs = bd;

	return ((rs & 0x1f) << 11) | ((gs & 0x3f) << 5) | (bs & 0x1f);
}

int main(int argc, char *argv[])
{
	int retv = 0, i, j;
	FILE *fin, *fout;
	unsigned char buf[64], *cpix;
	struct pngimage pim;
	struct cmdparam cmdl;
	unsigned short *ssd, *c_ssd;

	cmdl.width = 640;
	cmdl.height = 480;
	cmdl.cx = 200;
	cmdl.cy = 200;
	cmdl.png_in = NULL;
	cmdl.png_out = NULL;
	cmdl.ssd = NULL;
	retv = parse_cmdline(argc, argv, &cmdl);
	if (retv)
		return retv;

	fin = fopen(cmdl.png_in, "rb");
	if (!fin) {
		fprintf (stderr, "Input file \"%s\" error: %s\n", argv[1],
			strerror(errno));
		return 100;
	}
	fread(buf, 1, sizeof(buf), fin);
	if (png_sig_cmp(buf, 0, sizeof(buf)) != 0) {
		fprintf(stderr, "\"%s\" is not a valid png file!\n", argv[1]);
		retv = 200;
		goto exit_10;
	}
	rewind(fin);

	if ((retv = png_read(fin, &pim))) {
		fprintf(stderr, "Image read failed!\n");
		goto exit_20;
	}
	fclose(fin);
	fin = NULL;

	if ((retv = crop_image(&cmdl, &pim))) {
		fprintf(stderr, "Cannot crop image to: cx: %d, cy: %d, "
				"width: %d, height: %d\n", cmdl.cx, cmdl.cy,
				cmdl.width, cmdl.height);
		goto exit_20;
	};

	fout = fopen(cmdl.png_out, "wb");
	if (!fout) {
		fprintf(stderr, "Outputfile \"%s\" error: %s\n", argv[2],
			strerror(errno));
		retv = 104;
		goto exit_20;
	}
	png_write(fout, &pim);
	fclose(fout);
	fout = NULL;

	if (PIXBYTE(pim.bitdepth) == 3 && cmdl.ssd) {
		ssd = malloc(pim.width*pim.height*sizeof(unsigned short));
		if (!ssd) {
			fprintf(stderr, "Out of Memory!\n");
			retv = 10000;
			goto exit_20;
		}
		cpix = pim.area;
		c_ssd = ssd;
		for (i = 0; i < pim.height; i++) {
			for (j = 0; j < pim.width; j++) {
				*c_ssd = rgb2ssd(cpix);
				cpix += 3;
				c_ssd++;
			}
		}
		fout = fopen(cmdl.ssd, "wb");
		if (!fout) {
			fprintf(stderr, "Cannot open file: %s\n", cmdl.ssd);
			free(ssd);
			retv = 104;
			goto exit_20;
		}
		fwrite(ssd, sizeof(unsigned short), pim.height*pim.width, fout);
		fclose(fout);
		free(ssd);
	}

exit_20:
	if (pim.alloc) {
		munmap(pim.area, pim.height*pim.width*PIXBYTE(pim.bitdepth));
		free(pim.rows);
	}

exit_10:
	if (fin)
		fclose(fin);
	return retv;
}
