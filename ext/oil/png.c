#include <ruby.h>
#include <png.h>
#include "resample.h"

static ID id_read;

struct readerdata {
	png_structp png;
	png_infop info;
	VALUE source_io;
	uint32_t scale_width;
	uint32_t scale_height;
	int locked;
};

static void warning(png_structp png_ptr, png_const_charp message)
{
	rb_warning("libpng: %s", message);
}

static void error(png_structp png_ptr, png_const_charp message)
{
	rb_raise(rb_eRuntimeError, "libpng: %s", message);
}

static void read_data(png_structp png_ptr, png_bytep data, png_size_t length)
{
	size_t out_len;
	VALUE io, string;

	io = (VALUE)png_get_io_ptr(png_ptr);
	string = rb_funcall(io, id_read, 1, INT2FIX(length));
	Check_Type(string, T_STRING);

	out_len = RSTRING_LEN(string);
	if (out_len != length) {
		png_error(png_ptr, "IO returned wrong amount of data.");
		return;
	}

	memcpy(data, RSTRING_PTR(string), length);
}

static void flush_data_fn(png_structp png_ptr) {}

static void write_data_fn(png_structp png_ptr, png_bytep data, png_size_t length)
{
	rb_yield(rb_str_new((char *)data, length));
}

/* Ruby GC */

static void deallocate(struct readerdata *reader)
{
	png_destroy_read_struct(&reader->png, &reader->info, NULL);
	free(reader);
}

static void mark(struct readerdata *reader)
{
	if (!NIL_P(reader->source_io)) {
		rb_gc_mark(reader->source_io);
	}
}

static void allocate2(struct readerdata *reader)
{
	reader->png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, (png_error_ptr)error, (png_error_ptr)warning);
	reader->info = png_create_info_struct(reader->png);
}

static VALUE allocate(VALUE klass)
{
	struct readerdata *reader;
	VALUE self;

	self = Data_Make_Struct(klass, struct readerdata, mark, deallocate, reader);
	allocate2(reader);
	return self;
}

/* Helper that raises an exception if the reader is locked. */

static void raise_if_locked(struct readerdata *reader)
{
	if (reader->locked) {
		rb_raise(rb_eRuntimeError, "Can't modify a Reader after decompress started.");
	}
}

static VALUE initialize(VALUE self, VALUE io)
{
	struct readerdata *reader;

	Data_Get_Struct(self, struct readerdata, reader);

	if (reader->info) {
		png_destroy_read_struct(&reader->png, &reader->info, NULL);
		allocate2(reader);
		reader->locked = 0;
	}

	reader->source_io = io;
	png_set_read_fn(reader->png, (void*)io, read_data);
	png_read_info(reader->png, reader->info);

	png_set_packing(reader->png);
	png_set_strip_16(reader->png);
	png_set_expand(reader->png);
	png_read_update_info(reader->png, reader->info);

	reader->scale_width = png_get_image_width(reader->png, reader->info);
	reader->scale_height = png_get_image_height(reader->png, reader->info);
	return self;
}

/*
*  call-seq:
*     reader.width -> number
*
*  Retrieve the width of the image.
*/

static VALUE width(VALUE self)
{
	struct readerdata *reader;
	Data_Get_Struct(self, struct readerdata, reader);
	return INT2FIX(png_get_image_width(reader->png, reader->info));
}

/*
*  call-seq:
*     reader.height -> number
*
*  Retrieve the height of the image.
*/

static VALUE height(VALUE self)
{
	struct readerdata *reader;
	Data_Get_Struct(self, struct readerdata, reader);
	return INT2FIX(png_get_image_height(reader->png, reader->info));
}

/*
 *  call-seq:
 *     reader.scale_width -> number
 *
 *  Retrieve the width to which the image will be resized after decompression. A
 *  width of 0 means the image will remain at original width.
 */

static VALUE scale_width(VALUE self)
{
	struct readerdata *reader;
	Data_Get_Struct(self, struct readerdata, reader);
	return INT2FIX(reader->scale_width);
}

/*
 *  call-seq:
 *     reader.scale_width = number
 *
 *  Set the width to which the image will be resized after decompression. A
 *  width of 0 means the image will remain at original width.
 */

static VALUE set_scale_width(VALUE self, VALUE scale_width)
{
	struct readerdata *reader;
	Data_Get_Struct(self, struct readerdata, reader);
	raise_if_locked(reader);
	reader->scale_width = NUM2INT(scale_width);
	return scale_width;
}

/*
 *  call-seq:
 *     reader.scale_height -> number
 *
 *  Retrieve the height to which the image will be resized after decompression. A
 *  height of 0 means the image will remain at original height.
 */

static VALUE scale_height(VALUE self)
{
	struct readerdata *reader;
	Data_Get_Struct(self, struct readerdata, reader);
	return INT2FIX(reader->scale_height);
}

/*
 *  call-seq:
 *     reader.scale_height = number
 *
 *  Set the height to which the image will be resized after decompression. A
 *  height of 0 means the image will remain at original height.
 */

static VALUE set_scale_height(VALUE self, VALUE scale_height)
{
	struct readerdata *reader;
	Data_Get_Struct(self, struct readerdata, reader);
	raise_if_locked(reader);
	reader->scale_height = NUM2INT(scale_height);
	return scale_height;
}

struct each_args {
	struct readerdata *reader;
	png_structp wpng;
	png_infop winfo;
	unsigned char *inwidthbuf;
	unsigned char *outwidthbuf;
	unsigned char **scanlines;
	struct yscaler ys;
	struct xscaler xs;
};

static VALUE each_interlace(struct each_args *args)
{
	struct readerdata *reader;
	unsigned char *inwidthbuf, *outwidthbuf;
	uint32_t i, width, height, scaley;
	int cmp;
	struct xscaler *xs;

	reader = args->reader;
	xs = &args->xs;
	inwidthbuf = xscaler_psl_pos0(xs);
	outwidthbuf = args->outwidthbuf;
	scaley = reader->scale_height;
	cmp = png_get_channels(reader->png, reader->info);
	width = png_get_image_width(reader->png, reader->info);
	height = png_get_image_height(reader->png, reader->info);

	png_write_info(args->wpng, args->winfo);
	png_read_image(args->reader->png, (png_bytepp)args->scanlines);

	for (i=0; i<scaley; i++) {
		yscaler_prealloc_scale(height, scaley,
			(uint8_t **)args->scanlines, (uint8_t *)inwidthbuf,
			i, width, cmp, 0);
		xscaler_scale(xs, outwidthbuf);
		png_write_row(args->wpng, outwidthbuf);
	}
	png_write_end(args->wpng, args->winfo);
	return Qnil;
}

static VALUE each_interlace_none(struct each_args *args)
{
	struct readerdata *reader;
	unsigned char *inwidthbuf, *outwidthbuf, *yinbuf;
	struct xscaler *xs;
	struct yscaler *ys;
	uint32_t i, scaley;
	int cmp;

	reader = args->reader;
	xs = &args->xs;
	inwidthbuf = xscaler_psl_pos0(xs);
	outwidthbuf = args->outwidthbuf;
	ys = &args->ys;
	scaley = reader->scale_height;
	cmp = png_get_channels(reader->png, reader->info);

	png_write_info(args->wpng, args->winfo);

	for(i=0; i<scaley; i++) {
		while ((yinbuf = yscaler_next(ys))) {
			png_read_row(reader->png, inwidthbuf, NULL);
			xscaler_scale(xs, yinbuf);
		}
		yscaler_scale(ys, outwidthbuf, i, cmp, 0);
		png_write_row(args->wpng, outwidthbuf);
	}

	png_write_end(args->wpng, args->winfo);
	return Qnil;
}

/*
 * call-seq:
 *    reader.each(opts, &block) -> self
 *
 * Yields a series of binary strings that make up the output JPEG image.
 *
 * Options is a hash which may have the following symbols:
 *
 * :quality - JPEG quality setting. Betweein 0 and 100.
 * :markers - Custom markers to include in the output JPEG. Must be a hash where
 *   the keys are :APP[0-15] or :COM and the values are arrays of strings that
 *   will be inserted into the markers.
 */

static VALUE each(int argc, VALUE *argv, VALUE self)
{
	struct readerdata *reader;
	png_infop winfo;
	png_structp wpng;
	VALUE opts;
	int cmp, state;
	struct each_args args;
	uint32_t i, height, width;
	png_byte ctype;
	unsigned char **scanlines;
	size_t row_bytes;

	rb_scan_args(argc, argv, "01", &opts);

	Data_Get_Struct(self, struct readerdata, reader);

	raise_if_locked(reader);
	reader->locked = 1;

	wpng = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL,
		(png_error_ptr)error, (png_error_ptr)warning);
	winfo = png_create_info_struct(wpng);
	png_set_write_fn(wpng, 0, write_data_fn, flush_data_fn);

	cmp = png_get_channels(reader->png, reader->info);
	ctype = png_get_color_type(reader->png, reader->info);

	png_set_IHDR(wpng, winfo, reader->scale_width, reader->scale_height, 8,
		ctype, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
		PNG_FILTER_TYPE_DEFAULT);

	width = png_get_image_width(reader->png, reader->info);
	height = png_get_image_height(reader->png, reader->info);
	row_bytes = png_get_rowbytes(reader->png, reader->info);

	args.reader = reader;
	args.wpng = wpng;
	args.winfo = winfo;
	args.inwidthbuf = malloc(row_bytes);
	args.outwidthbuf = malloc(reader->scale_width * cmp);

	xscaler_init(&args.xs, width, reader->scale_width, cmp, 0);

	if (png_get_interlace_type(reader->png, reader->info) == PNG_INTERLACE_NONE) {
		yscaler_init(&args.ys, height, reader->scale_height,
			reader->scale_width * cmp);
		rb_protect((VALUE(*)(VALUE))each_interlace_none, (VALUE)&args, &state);
		yscaler_free(&args.ys);
	} else {
		scanlines = malloc(height * sizeof(unsigned char *));
		for (i=0; i<height; i++) {
			scanlines[i] = malloc(row_bytes);
		}

		args.scanlines = scanlines;
		rb_protect((VALUE(*)(VALUE))each_interlace, (VALUE)&args, &state);

		for (i=0; i<height; i++) {
			free(scanlines[i]);
		}
		free(scanlines);
	}

	xscaler_free(&args.xs);
	free(args.inwidthbuf);
	free(args.outwidthbuf);
	png_destroy_write_struct(&wpng, &winfo);

	if (state) {
		rb_jump_tag(state);
	}

	return self;
}

void Init_png()
{
	VALUE mOil, cPNGReader;
	mOil = rb_const_get(rb_cObject, rb_intern("Oil"));
	cPNGReader = rb_define_class_under(mOil, "PNGReader", rb_cObject);
	rb_define_alloc_func(cPNGReader, allocate);
	rb_define_method(cPNGReader, "initialize", initialize, 1);
	rb_define_method(cPNGReader, "width", width, 0);
	rb_define_method(cPNGReader, "height", height, 0);
	rb_define_method(cPNGReader, "scale_width", scale_width, 0);
	rb_define_method(cPNGReader, "scale_width=", set_scale_width, 1);
	rb_define_method(cPNGReader, "scale_height", scale_height, 0);
	rb_define_method(cPNGReader, "scale_height=", set_scale_height, 1);
	rb_define_method(cPNGReader, "each", each, -1);
	id_read = rb_intern("read");
}
