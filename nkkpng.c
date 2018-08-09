#include <stdio.h>
#include <png.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <sys/mman.h>

#define PIXBYTE(bitdepth) (3*((bitdepth-1)/8+1))

struct pngimage {
	png_uint_32 height, width;
	int bitdepth, colortyp;
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
	char *draw;
	png_bytep *c_row;
	int i, pixbytes, stripe;

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

	pixbytes = PIXBYTE(im->bitdepth);
	draw = mmap(NULL, im->width*im->height*pixbytes, PROT_READ|PROT_WRITE,
		MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
	if (!draw) {
		fprintf(stderr, "Out of Memory!\n");
		retv = 10000;
		goto exit_16;
	}
	im->rows = malloc(im->height*sizeof(png_bytep *));
	if (!im->rows) {
		fprintf(stderr, "Out of Memory!\n");
		retv = 1000;
		goto exit_18;
	}
	im->alloc = 1;
	stripe = 0;
	for (c_row = im->rows, i = 0; i < im->height; i++, c_row++) {
		*c_row = (png_bytep)(draw + stripe);
		stripe += im->width*pixbytes;
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

int main(int argc, char *argv[])
{
	int retv = 0;
	FILE *fin, *fout;
	unsigned char buf[64];
	struct pngimage pim;

	if (argc < 3) {
		fprintf(stderr, "Usage: %s input output\n", argv[0]);
		return 1;
	}
	fin = fopen(argv[1], "rb");
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

	fout = fopen(argv[2], "wb");
	if (!fout) {
		fprintf(stderr, "Outputfile \"%s\" error: %s\n", argv[2],
			strerror(errno));
		retv = 104;
		goto exit_20;
	}
	png_write(fout, &pim);
	fclose(fout);

exit_20:
	if (pim.alloc) {
		munmap(pim.rows[0], pim.height*pim.width*PIXBYTE(pim.bitdepth));
		free(pim.rows);
	}

exit_10:
	if (fin)
		fclose(fin);
	return retv;
}
