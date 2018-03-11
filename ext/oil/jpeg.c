#include <ruby.h>
#include <ruby/st.h>
#include <jpeglib.h>
#include "resample.h"

#define READ_SIZE 1024
#define WRITE_SIZE 1024

static ID id_GRAYSCALE, id_RGB, id_YCbCr, id_CMYK, id_YCCK, id_RGBX, id_UNKNOWN;
static ID id_APP0, id_APP1, id_APP2, id_APP3, id_APP4, id_APP5, id_APP6,
	id_APP7, id_APP8, id_APP9, id_APP10, id_APP11, id_APP12, id_APP13,
	id_APP14, id_APP15, id_COM;
static ID id_read;

static VALUE sym_quality, sym_markers;

/* Color Space Conversion Helpers. */

static ID j_color_space_to_id(J_COLOR_SPACE cs)
{
	switch (cs) {
	case JCS_GRAYSCALE:
		return id_GRAYSCALE;
	case JCS_RGB:
		return id_RGB;
	case JCS_YCbCr:
		return id_YCbCr;
	case JCS_CMYK:
		return id_CMYK;
	case JCS_YCCK:
		return id_YCCK;
#ifdef JCS_EXTENSIONS
	case JCS_EXT_RGBX:
		return id_RGBX;
#endif
	default:
		return id_UNKNOWN;
	}
}

static J_COLOR_SPACE sym_to_j_color_space(VALUE sym)
{
	ID rb = SYM2ID(sym);

	if (rb == id_GRAYSCALE) {
		return JCS_GRAYSCALE;
	} else if (rb == id_RGB) {
		return JCS_RGB;
	} else if (rb == id_YCbCr) {
		return JCS_YCbCr;
	} else if (rb == id_CMYK) {
		return JCS_CMYK;
	} else if (rb == id_YCCK) {
		return JCS_YCCK;
	} else if (rb == id_RGBX) {
#ifdef JCS_EXTENSIONS
		return JCS_EXT_RGBX;
#else
		return JCS_RGB;
#endif
	}
	rb_raise(rb_eRuntimeError, "Color space not recognized.");
}

static int sym_to_marker_code(VALUE sym)
{
	ID rb = SYM2ID(sym);

	if (rb == id_COM) {
		return JPEG_COM;
	} else if (rb == id_APP0) {
		return JPEG_APP0;
	} else if (rb == id_APP1) {
		return JPEG_APP0 + 1;
	} else if (rb == id_APP2) {
		return JPEG_APP0 + 2;
	} else if (rb == id_APP3) {
		return JPEG_APP0 + 3;
	} else if (rb == id_APP4) {
		return JPEG_APP0 + 4;
	} else if (rb == id_APP5) {
		return JPEG_APP0 + 5;
	} else if (rb == id_APP6) {
		return JPEG_APP0 + 6;
	} else if (rb == id_APP7) {
		return JPEG_APP0 + 7;
	} else if (rb == id_APP8) {
		return JPEG_APP0 + 8;
	} else if (rb == id_APP9) {
		return JPEG_APP0 + 9;
	} else if (rb == id_APP10) {
		return JPEG_APP0 + 10;
	} else if (rb == id_APP11) {
		return JPEG_APP0 + 11;
	} else if (rb == id_APP12) {
		return JPEG_APP0 + 12;
	} else if (rb == id_APP13) {
		return JPEG_APP0 + 13;
	} else if (rb == id_APP14) {
		return JPEG_APP0 + 14;
	} else if (rb == id_APP15) {
		return JPEG_APP0 + 15;
	}
	rb_raise(rb_eRuntimeError, "Marker code not recognized.");
}

static VALUE marker_code_to_sym(int marker_code)
{
	switch(marker_code) {
	case JPEG_COM:
		return ID2SYM(id_COM);
	case JPEG_APP0:
		return ID2SYM(id_APP0);
	case JPEG_APP0 + 1:
		return ID2SYM(id_APP1);
	case JPEG_APP0 + 2:
		return ID2SYM(id_APP2);
	case JPEG_APP0 + 3:
		return ID2SYM(id_APP3);
	case JPEG_APP0 + 4:
		return ID2SYM(id_APP4);
	case JPEG_APP0 + 5:
		return ID2SYM(id_APP5);
	case JPEG_APP0 + 6:
		return ID2SYM(id_APP6);
	case JPEG_APP0 + 7:
		return ID2SYM(id_APP7);
	case JPEG_APP0 + 8:
		return ID2SYM(id_APP8);
	case JPEG_APP0 + 9:
		return ID2SYM(id_APP9);
	case JPEG_APP0 + 10:
		return ID2SYM(id_APP10);
	case JPEG_APP0 + 11:
		return ID2SYM(id_APP11);
	case JPEG_APP0 + 12:
		return ID2SYM(id_APP12);
	case JPEG_APP0 + 13:
		return ID2SYM(id_APP13);
	case JPEG_APP0 + 14:
		return ID2SYM(id_APP14);
	case JPEG_APP0 + 15:
		return ID2SYM(id_APP15);
	}
	rb_raise(rb_eRuntimeError, "Marker code not recognized.");
}

/* JPEG Error Handler -- raise a ruby exception. */

void output_message(j_common_ptr cinfo)
{
	char buffer[JMSG_LENGTH_MAX];
	cinfo->err->format_message(cinfo, buffer);
	rb_warning("jpeglib: %s", buffer);
}

static void error_exit(j_common_ptr dinfo)
{
	char buffer[JMSG_LENGTH_MAX];
	(*dinfo->err->format_message) (dinfo, buffer);
	rb_raise(rb_eRuntimeError, "jpeglib: %s", buffer);
}

/* JPEG Data Source */

struct readerdata {
	struct jpeg_decompress_struct dinfo;
	struct jpeg_source_mgr mgr;
	struct jpeg_error_mgr jerr;
	int locked;
	VALUE source_io;
	VALUE buffer;
	uint32_t scale_width;
	uint32_t scale_height;
};

static void null_jdecompress(j_decompress_ptr dinfo) {}

static boolean fill_input_buffer(j_decompress_ptr dinfo)
{
	VALUE string;
	long strl;
	struct readerdata *reader;

	reader = (struct readerdata *)dinfo;

	string = rb_funcall(reader->source_io, id_read, 1, INT2FIX(READ_SIZE));
	Check_Type(string, T_STRING);

	strl = RSTRING_LEN(string);
	if (strl > READ_SIZE) {
		rb_raise(rb_eRuntimeError, "IO returned too much data.");
	}

	if (!strl) {
		string = rb_str_new2("\xFF\xD9");
		strl = 2;
	}

	reader->buffer = string;
	reader->mgr.bytes_in_buffer = strl;
	reader->mgr.next_input_byte = (unsigned char *)RSTRING_PTR(string);

	return TRUE;
}

static void skip_input_data(j_decompress_ptr dinfo, long num_bytes)
{
	struct jpeg_source_mgr * src = dinfo->src;

	if (num_bytes > 0) {
		while (num_bytes > (long) src->bytes_in_buffer) {
			num_bytes -= (long) src->bytes_in_buffer;
			(void) (*src->fill_input_buffer) (dinfo);
		}
		src->next_input_byte += (size_t) num_bytes;
		src->bytes_in_buffer -= (size_t) num_bytes;
	}
}

/* Ruby GC */

static void deallocate(struct readerdata *reader)
{
	jpeg_destroy_decompress(&reader->dinfo);
	free(reader);
}

static void mark(struct readerdata *reader)
{
	if (!NIL_P(reader->source_io)) {
		rb_gc_mark(reader->source_io);
	}

	if (!NIL_P(reader->buffer)) {
		rb_gc_mark(reader->buffer);
	}
}

static VALUE allocate(VALUE klass)
{
	struct readerdata *reader;
	VALUE self;
	self = Data_Make_Struct(klass, struct readerdata, mark, deallocate, reader);

	jpeg_std_error(&reader->jerr);
	reader->jerr.error_exit = error_exit;
	reader->jerr.output_message = output_message;
	reader->dinfo.err = &reader->jerr;
	reader->mgr.init_source = null_jdecompress;
	reader->mgr.fill_input_buffer = fill_input_buffer;
	reader->mgr.skip_input_data = skip_input_data;
	reader->mgr.resync_to_restart = jpeg_resync_to_restart;
	reader->mgr.term_source = null_jdecompress;
	return self;
}

/* Helper that raises an exception if the reader is locked. */

static void raise_if_locked(struct readerdata *reader)
{
	if (reader->locked) {
		rb_raise(rb_eRuntimeError, "Can't modify a Reader after decompress started.");
	}
}

/*
 *  call-seq:
 *     Reader.new(io_in [, markers]) -> reader
 *
 *  Creates a new JPEG Reader. +io_in+ must be an IO-like object that responds
 *  to read(size).
 *
 *  +markers+ should be an array of valid JPEG header marker symbols. Valid
 *  symbols are :APP0 through :APP15 and :COM.
 *
 *  If performance is important, you can avoid reading any header markers by
 *  supplying an empty array, [].
 *
 *  When markers are not specified, we read all known JPEG markers.
 *
 *     io = File.open("image.jpg", "r")
 *     reader = Oil::JPEGReader.new(io)
 *
 *     io = File.open("image.jpg", "r")
 *     reader = Oil::JPEGReader.new(io, [:APP1, :APP2])
 */

static VALUE initialize(int argc, VALUE *argv, VALUE self)
{
	struct readerdata *reader;
	VALUE io, markers;
	struct jpeg_decompress_struct *dinfo;
	int i, marker_code;

	Data_Get_Struct(self, struct readerdata, reader);
	dinfo = &reader->dinfo;

	/* If source_io has already been set, then this is a re-used jpeg reader
	 * object. This means we need to abort the previous decompress to
	 * prevent memory leaks.
	 */
	if (reader->source_io) {
		jpeg_abort_decompress(dinfo);
	} else {
		jpeg_create_decompress(dinfo);
	}

	dinfo->src = &reader->mgr;

	rb_scan_args(argc, argv, "11", &io, &markers);
	reader->source_io = io;
	reader->mgr.bytes_in_buffer = 0;

	if(!NIL_P(markers)) {
		Check_Type(markers, T_ARRAY);
		for (i=0; i<RARRAY_LEN(markers); i++) {
			if (!SYMBOL_P(RARRAY_PTR(markers)[i])) {
				rb_raise(rb_eTypeError, "Marker code is not a symbol.");
			}
			marker_code = sym_to_marker_code(RARRAY_PTR(markers)[i]);
			jpeg_save_markers(dinfo, marker_code, 0xFFFF);
		}
	}

	/* Be warned that this can raise a ruby exception and longjmp away. */
	jpeg_read_header(dinfo, TRUE);

	jpeg_calc_output_dimensions(dinfo);

	return self;
}

/*
 *  call-seq:
 *     reader.num_components -> number
 *
 *  Retrieve the number of components per pixel as indicated by the image
 *  header.
 *
 *  This may differ from the number of components that will be returned by the
 *  decompressor if we ask for a color space transformation.
 */

static VALUE num_components(VALUE self)
{
	struct jpeg_decompress_struct * dinfo;
	Data_Get_Struct(self, struct jpeg_decompress_struct, dinfo);
	return INT2FIX(dinfo->num_components);
}

/*
 *  call-seq:
 *     reader.output_components -> number
 *
 *  Retrieve the number of bytes per pixel that will be in the output image.
 *
 *  Not all bytes will necessarily have data, since some color spaces have
 *  padding.
 */

static VALUE output_components(VALUE self)
{
	struct jpeg_decompress_struct * dinfo;
	Data_Get_Struct(self, struct jpeg_decompress_struct, dinfo);
	return INT2FIX(dinfo->output_components);
}

/*
 *  call-seq:
 *     reader.out_color_components -> number
 *
 *  Retrieve the number of components in the output color space.
 *
 *  Some color spaces have padding, so this may not accurately represent the
 *  size of output pixels.
 */

static VALUE out_color_components(VALUE self)
{
	struct jpeg_decompress_struct * dinfo;
	Data_Get_Struct(self, struct jpeg_decompress_struct, dinfo);
	return INT2FIX(dinfo->out_color_components);
}

/*
 *  call-seq:
 *     reader.jpeg_color_space -> symbol
 *
 *  Returns a symbol representing the color model in which the JPEG is stored,
 *  as indicated by the image header.
 *
 *  Possible color models are: :GRAYSCALE, :RGB, :YCbCr, :CMYK, and :YCCK. This
 *  method will return :UNKNOWN if the color model is not recognized.
 *
 *  This may differ from the color space that will be returned by the
 *  decompressor if we ask for a color space transformation.
 */

static VALUE jpeg_color_space(VALUE self)
{
	struct jpeg_decompress_struct * dinfo;
	ID id;

	Data_Get_Struct(self, struct jpeg_decompress_struct, dinfo);
	id = j_color_space_to_id(dinfo->jpeg_color_space);

	return ID2SYM(id);
}

/*
 *  call-seq:
 *     reader.out_color_space -> symbol
 *
 *  Returns a symbol representing the color model to which the image will be
 *  converted on decompress.
 */

static VALUE out_color_space(VALUE self)
{
	struct jpeg_decompress_struct * dinfo;
	ID id;

	Data_Get_Struct(self, struct jpeg_decompress_struct, dinfo);
	id = j_color_space_to_id(dinfo->out_color_space);

	return ID2SYM(id);
}

/*
 *  call-seq:
 *     reader.out_color_space = symbol
 *
 *  Set the color model to which the image will be converted on decompress.
 */

static VALUE set_out_color_space(VALUE self, VALUE cs)
{
	struct readerdata *reader;

	Data_Get_Struct(self, struct readerdata, reader);
	raise_if_locked(reader);

	reader->dinfo.out_color_space = sym_to_j_color_space(cs);
	jpeg_calc_output_dimensions(&reader->dinfo);
	return cs;
}

/*
 *  call-seq:
 *     reader.image_width -> number
 *
 *  The width of the of the image as indicated by the header.
 *
 *  This may differ from the width of the image that will be returned by the
 *  decompressor if we request DCT scaling.
 */

static VALUE image_width(VALUE self)
{
	struct jpeg_decompress_struct * dinfo;
	Data_Get_Struct(self, struct jpeg_decompress_struct, dinfo);
	return INT2FIX(dinfo->image_width);
}

/*
 *  call-seq:
 *     reader.image_height -> number
 *
 *  The height of the image as indicated by the header.
 *
 *  This may differ from the height of the image that will be returned by the
 *  decompressor if we request DCT scaling.
 */

static VALUE image_height(VALUE self)
{
	struct jpeg_decompress_struct * dinfo;
	Data_Get_Struct(self, struct jpeg_decompress_struct, dinfo);
	return INT2FIX(dinfo->image_height);
}

/*
 *  call-seq:
 *     reader.output_width -> number
 *
 *  The width of the of the image that will be output by the decompressor.
 */

static VALUE output_width(VALUE self)
{
	struct jpeg_decompress_struct * dinfo;
	Data_Get_Struct(self, struct jpeg_decompress_struct, dinfo);
	return INT2FIX(dinfo->output_width);
}

/*
 *  call-seq:
 *     reader.image_height -> number
 *
 *  The height of the image that will be output by the decompressor.
 */

static VALUE output_height(VALUE self)
{
	struct jpeg_decompress_struct * dinfo;
	Data_Get_Struct(self, struct jpeg_decompress_struct, dinfo);
	return INT2FIX(dinfo->output_height);
}

/*
 *  call-seq:
 *     reader.markers -> hash
 *
 *  Get a hash of raw marker data from the JPEG.
 *
 *  The keys in the hash are the marker codes as symbols. The values are arrays.
 *
 *  Arrays since there may be multiple instances of a single marker in a JPEG
 *  marker.
 */

static VALUE markers(VALUE self)
{
	struct jpeg_decompress_struct *dinfo;
	jpeg_saved_marker_ptr marker;
	VALUE hash, ary, key, val;

	hash = rb_hash_new();

	Data_Get_Struct(self, struct jpeg_decompress_struct, dinfo);

	for (marker=dinfo->marker_list; marker; marker=marker->next) {
		key = marker_code_to_sym(marker->marker);
		ary = rb_hash_aref(hash, key);
		if (NIL_P(ary)) {
			ary = rb_ary_new();
			rb_hash_aset(hash, key, ary);
		}
		val = rb_str_new((char *)marker->data, marker->data_length);
		rb_ary_push(ary, val);
	}

	return hash;
}

/*
 *  call-seq:
 *     reader.scale_num -> number
 *
 *  Retrieve the numerator of the fraction by which the JPEG will be scaled as
 *  it is read. This is always 1 for libjpeg version 6b. In version 8b this can
 *  be 1 to 16.
 */

static VALUE scale_num(VALUE self)
{
	struct jpeg_decompress_struct *dinfo;
	Data_Get_Struct(self, struct jpeg_decompress_struct, dinfo);
	return INT2FIX(dinfo->scale_num);
}

/*
 *  call-seq:
 *     reader.scale_num = number
 *
 *  Set the numerator of the fraction by which the JPEG will be scaled as it is
 *  read. This must always be 1 for libjpeg version 6b. In version 8b this can
 *  be set to 1 through 16.
 */

static VALUE set_scale_num(VALUE self, VALUE scale_num)
{
	struct readerdata *reader;

	Data_Get_Struct(self, struct readerdata, reader);
	raise_if_locked(reader);

	reader->dinfo.scale_num = NUM2INT(scale_num);
	jpeg_calc_output_dimensions(&reader->dinfo);
	return scale_num;
}

/*
 *  call-seq:
 *     reader.scale_denom -> number
 *
 *  Retrieve the denominator of the fraction by which the JPEG will be scaled as
 *  it is read. This is 1, 2, 4, or 8 for libjpeg version 6b. In version 8b this
 *  is always the source DCT size, which is 8 for baseline JPEG.
 */

static VALUE scale_denom(VALUE self)
{
	struct jpeg_decompress_struct *dinfo;
	Data_Get_Struct(self, struct jpeg_decompress_struct, dinfo);
	return INT2FIX(dinfo->scale_denom);
}

/*
 *  call-seq:
 *     reader.scale_denom = number
 *
 *  Set the denominator of the fraction by which the JPEG will be scaled as it
 *  is read. This can be set to 1, 2, 4, or 8 for libjpeg version 6b. In version
 *  8b this must always be the source DCT size, which is 8 for baseline JPEG.
 *
 *  Prior to version 1.2, libjpeg-turbo will not scale down images on
 *  decompression, and this option will do nothing.
 */

static VALUE set_scale_denom(VALUE self, VALUE scale_denom)
{
	struct readerdata *reader;

	Data_Get_Struct(self, struct readerdata, reader);
	raise_if_locked(reader);

	reader->dinfo.scale_denom = NUM2INT(scale_denom);
	jpeg_calc_output_dimensions(&reader->dinfo);
	return scale_denom;
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

/* JPEG Data Destination */

struct writerdata {
	struct jpeg_compress_struct cinfo;
	struct jpeg_destination_mgr mgr;
	VALUE buffer;
};

static void init_destination(j_compress_ptr cinfo)
{
	struct writerdata *writer;

	writer = (struct writerdata *)cinfo;
	writer->buffer = rb_str_new(NULL, WRITE_SIZE);
	writer->mgr.next_output_byte = (JOCTET *)RSTRING_PTR(writer->buffer);
	writer->mgr.free_in_buffer = WRITE_SIZE;
}

static boolean empty_output_buffer(j_compress_ptr cinfo)
{
	struct writerdata *writer;

	writer = (struct writerdata *)cinfo;
	rb_yield(writer->buffer);
	init_destination(cinfo);
	return TRUE;
}

static void term_destination(j_compress_ptr cinfo)
{
	struct writerdata *writer;
	size_t datacount;

	writer = (struct writerdata *)cinfo;
	datacount = WRITE_SIZE - writer->mgr.free_in_buffer;

	if (datacount > 0) {
		rb_str_set_len(writer->buffer, datacount);
		rb_yield(writer->buffer);
	}
}

static int markerhash_each(VALUE marker_code_v, VALUE marker_ary, VALUE cinfo_v)
{
	struct jpeg_compress_struct *cinfo;
	int i, marker_code;
	size_t strl;
	VALUE marker;

	cinfo = (struct jpeg_compress_struct *)cinfo_v;
	marker_code = sym_to_marker_code(marker_code_v);

	Check_Type(marker_ary, T_ARRAY);
	for (i=0; i<RARRAY_LEN(marker_ary); i++) {
		marker = rb_ary_entry(marker_ary, i);
		Check_Type(marker, T_STRING);
		strl = RSTRING_LEN(marker);
		jpeg_write_marker(cinfo, marker_code, (JOCTET *)RSTRING_PTR(marker), strl);
	}

	return ST_CONTINUE;
}

struct write_jpeg_args {
	VALUE opts;
	struct readerdata *reader;
	struct writerdata *writer;
	unsigned char *outwidthbuf;
	struct yscaler ys;
	struct xscaler xs;
};

static VALUE each2(struct write_jpeg_args *args)
{
	struct writerdata *writer;
	struct jpeg_decompress_struct *dinfo;
	struct jpeg_compress_struct *cinfo;
	unsigned char *outwidthbuf, *yinbuf, *psl_pos0;
	uint32_t i, scalex, scaley;
	VALUE quality, markers;
	int cmp, filler;

	writer = args->writer;
	outwidthbuf = args->outwidthbuf;
	dinfo = &args->reader->dinfo;
	cinfo = &writer->cinfo;
	scalex = args->reader->scale_width;
	scaley = args->reader->scale_height;

	cmp = dinfo->output_components;
#ifdef JCS_EXTENSIONS
	filler = dinfo->out_color_space == JCS_EXT_RGBX;
#else
	filler = 0;
#endif

	writer->mgr.init_destination = init_destination;
	writer->mgr.empty_output_buffer = empty_output_buffer;
	writer->mgr.term_destination = term_destination;
	writer->cinfo.dest = &writer->mgr;
	writer->cinfo.image_width = scalex;
	writer->cinfo.image_height = scaley;
	writer->cinfo.in_color_space = dinfo->out_color_space;
	writer->cinfo.input_components = cmp;

	psl_pos0 = xscaler_psl_pos0(&args->xs);

	jpeg_set_defaults(cinfo);

	if (!NIL_P(args->opts)) {
		quality = rb_hash_aref(args->opts, sym_quality);
		if (!NIL_P(quality)) {
			jpeg_set_quality(cinfo, FIX2INT(quality), FALSE);
		}
	}

	jpeg_start_compress(cinfo, TRUE);
	jpeg_start_decompress(dinfo);

	if (!NIL_P(args->opts)) {
		markers = rb_hash_aref(args->opts, sym_markers);
		if (!NIL_P(markers)) {
			Check_Type(markers, T_HASH);
			rb_hash_foreach(markers, markerhash_each, (VALUE)cinfo);
		}
	}

	for(i=0; i<scaley; i++) {
		while ((yinbuf = yscaler_next(&args->ys))) {
			jpeg_read_scanlines(dinfo, (JSAMPARRAY)&psl_pos0, 1);
			xscaler_scale(&args->xs, yinbuf);
		}
		yscaler_scale(&args->ys, outwidthbuf, i, cmp, filler);
		jpeg_write_scanlines(cinfo, (JSAMPARRAY)&outwidthbuf, 1);
	}

	jpeg_finish_compress(cinfo);

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
	struct writerdata writer;
	int cmp, state, filler;
	struct write_jpeg_args args;
	unsigned char *outwidthbuf;
	uint32_t width_in, width_out;
	VALUE opts;

	rb_scan_args(argc, argv, "01", &opts);

	Data_Get_Struct(self, struct readerdata, reader);

	if (!reader->scale_width) {
		reader->scale_width = reader->dinfo.output_width;
	}
	if (!reader->scale_height) {
		reader->scale_height = reader->dinfo.output_height;
	}

	writer.cinfo.err = &reader->jerr;
	jpeg_create_compress(&writer.cinfo);

#ifdef JCS_EXTENSIONS
	filler = reader->dinfo.out_color_space == JCS_EXT_RGBX;
#else
	filler = 0;
#endif
	cmp = reader->dinfo.output_components;
	width_in = reader->dinfo.output_width;
	width_out = reader->scale_width;
	outwidthbuf = malloc(width_out * cmp);
	xscaler_init(&args.xs, width_in, width_out, cmp, filler);
	yscaler_init(&args.ys, reader->dinfo.output_height, reader->scale_height,
		width_out * cmp);

	args.reader = reader;
	args.opts = opts;
	args.writer = &writer;
	args.outwidthbuf = outwidthbuf;
	reader->locked = 1;
	rb_protect((VALUE(*)(VALUE))each2, (VALUE)&args, &state);

	yscaler_free(&args.ys);
	xscaler_free(&args.xs);
	free(outwidthbuf);
	jpeg_destroy_compress(&writer.cinfo);

	if (state) {
		rb_jump_tag(state);
	}

	return self;
}

/*
 * Document-class: Oil::JPEGReader
 *
 * Read a compressed JPEG image given an IO object.
 */

void Init_jpeg()
{
	VALUE mOil, cJPEGReader;

	mOil = rb_const_get(rb_cObject, rb_intern("Oil"));

	cJPEGReader = rb_define_class_under(mOil, "JPEGReader", rb_cObject);
	rb_define_alloc_func(cJPEGReader, allocate);
	rb_define_method(cJPEGReader, "initialize", initialize, -1);
	rb_define_method(cJPEGReader, "markers", markers, 0);
	rb_define_method(cJPEGReader, "jpeg_color_space", jpeg_color_space, 0);
	rb_define_method(cJPEGReader, "out_color_space", out_color_space, 0);
	rb_define_method(cJPEGReader, "out_color_space=", set_out_color_space, 1);
	rb_define_method(cJPEGReader, "num_components", num_components, 0);
	rb_define_method(cJPEGReader, "output_components", output_components, 0);
	rb_define_method(cJPEGReader, "out_color_components", out_color_components, 0);
	rb_define_method(cJPEGReader, "image_width", image_width, 0);
	rb_define_method(cJPEGReader, "image_height", image_height, 0);
	rb_define_method(cJPEGReader, "output_width", output_width, 0);
	rb_define_method(cJPEGReader, "output_height", output_height, 0);
	rb_define_method(cJPEGReader, "each", each, -1);
	rb_define_method(cJPEGReader, "scale_num", scale_num, 0);
	rb_define_method(cJPEGReader, "scale_num=", set_scale_num, 1);
	rb_define_method(cJPEGReader, "scale_denom", scale_denom, 0);
	rb_define_method(cJPEGReader, "scale_denom=", set_scale_denom, 1);
	rb_define_method(cJPEGReader, "scale_width", scale_width, 0);
	rb_define_method(cJPEGReader, "scale_width=", set_scale_width, 1);
	rb_define_method(cJPEGReader, "scale_height", scale_height, 0);
	rb_define_method(cJPEGReader, "scale_height=", set_scale_height, 1);

	id_GRAYSCALE = rb_intern("GRAYSCALE");
	id_RGB = rb_intern("RGB");
	id_YCbCr = rb_intern("YCbCr");
	id_CMYK = rb_intern("CMYK");
	id_YCCK = rb_intern("YCCK");
	id_RGBX = rb_intern("RGBX");
	id_UNKNOWN = rb_intern("UNKNOWN");
	id_APP0 = rb_intern("APP0");
	id_APP1 = rb_intern("APP1");
	id_APP2 = rb_intern("APP2");
	id_APP3 = rb_intern("APP3");
	id_APP4 = rb_intern("APP4");
	id_APP5 = rb_intern("APP5");
	id_APP6 = rb_intern("APP6");
	id_APP7 = rb_intern("APP7");
	id_APP8 = rb_intern("APP8");
	id_APP9 = rb_intern("APP9");
	id_APP10 = rb_intern("APP10");
	id_APP11 = rb_intern("APP11");
	id_APP12 = rb_intern("APP12");
	id_APP13 = rb_intern("APP13");
	id_APP14 = rb_intern("APP14");
	id_APP15 = rb_intern("APP15");
	id_COM = rb_intern("COM");
	id_read = rb_intern("read");

	sym_quality = ID2SYM(rb_intern("quality"));
	sym_markers = ID2SYM(rb_intern("markers"));
}
