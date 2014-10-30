#include <ruby.h>
#include <png.h>
#include "resample.h"
#include "yscaler.h"

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
}

static void mark(struct readerdata *reader)
{
	if (!NIL_P(reader->source_io)) {
		rb_gc_mark(reader->source_io);
	}
}

static VALUE allocate(VALUE klass)
{
	struct readerdata *reader;
	VALUE self;

	self = Data_Make_Struct(klass, struct readerdata, mark, deallocate, reader);
	reader->png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, (png_error_ptr)error, (png_error_ptr)warning);
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

	if (reader->source_io) {
		png_destroy_read_struct(&reader->png, &reader->info, NULL);
		reader->png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, (png_error_ptr)error, (png_error_ptr)warning);
		reader->locked = 0;
	}

	reader->source_io = io;
	reader->info = png_create_info_struct(reader->png);
	png_set_read_fn(reader->png, (void*)io, read_data);
	png_read_info(reader->png, reader->info);
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

struct write_png_args {
	VALUE opts;
	struct readerdata *reader;
	unsigned char *inwidthbuf;
	unsigned char *outwidthbuf;
	struct yscaler *ys;
	png_structp png;
	png_infop info;
};

static VALUE each2(struct write_png_args *args)
{
	png_structp png;
	png_infop info;
	png_byte ctype;
	struct readerdata *reader;
	unsigned char *inwidthbuf, *outwidthbuf, *yinbuf;
	struct yscaler *ys;
	uint32_t i, scalex, scaley;
	int cmp;

	reader = args->reader;
	png = args->png;
	info = args->info;
	inwidthbuf = args->inwidthbuf;
	outwidthbuf = args->outwidthbuf;
	ys = args->ys;
	scalex = args->reader->scale_width;
	scaley = args->reader->scale_height;

	cmp = png_get_channels(reader->png, reader->info);
	png_set_write_fn(png, 0, write_data_fn, flush_data_fn);
	ctype = png_get_color_type(reader->png, reader->info);

	png_set_IHDR(png, info, scalex, scaley, 8, ctype, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
	png_write_info(png, info);

	for(i=0; i<scaley; i++) {
		while ((yinbuf = yscaler_next(ys))) {
			png_read_row(reader->png, inwidthbuf, NULL);
			xscale(inwidthbuf, png_get_image_width(reader->png, reader->info), yinbuf, scalex, cmp, 0);
		}
		yscaler_scale(ys, outwidthbuf, scalex, cmp, 0);
		png_write_row(png, outwidthbuf);
	}

	png_write_end(png, info);

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
	int cmp, state;
	struct write_png_args args;
	unsigned char *inwidthbuf, *outwidthbuf;
	struct yscaler ys;
	VALUE opts;
	png_structp png;
	png_infop info;

	rb_scan_args(argc, argv, "01", &opts);

	Data_Get_Struct(self, struct readerdata, reader);

	raise_if_locked(reader);
	reader->locked = 1;

	png_set_packing(reader->png);
	png_set_strip_16(reader->png);
	png_set_expand(reader->png);
	png_read_update_info(reader->png, reader->info);

	if (!reader->scale_width) {
		reader->scale_width = png_get_image_width(reader->png, reader->info);
	}
	if (!reader->scale_height) {
		reader->scale_height = png_get_image_height(reader->png, reader->info);
	}

	png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL,
		(png_error_ptr)error, (png_error_ptr)warning);
	info = png_create_info_struct(png);

	inwidthbuf = malloc(png_get_rowbytes(reader->png, reader->info));
	cmp = png_get_channels(reader->png, reader->info);
	outwidthbuf = malloc(reader->scale_width * cmp);
	yscaler_init(&ys, png_get_image_height(reader->png, reader->info),
		reader->scale_height, reader->scale_width * cmp);

	args.reader = reader;
	args.opts = opts;
	args.png = png;
	args.info = info;
	args.inwidthbuf = inwidthbuf;
	args.outwidthbuf = outwidthbuf;
	args.ys = &ys;
	rb_protect((VALUE(*)(VALUE))each2, (VALUE)&args, &state);

	yscaler_free(&ys);
	free(inwidthbuf);
	free(outwidthbuf);
	png_destroy_write_struct(&png, &info);

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
