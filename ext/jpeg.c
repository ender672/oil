#include "oil.h"
#include <stdio.h>
#include <jpeglib.h>
#include <setjmp.h>
#include <string.h>
#include <stdlib.h>

/* In/out buffers will use this size */

#define BUFSIZE 8192

#define ICC_MARKER      (JPEG_APP0+2)   // ICC profile marker


/* extend jpeg structs
 *
 * Placing longjmp buffers in the per-image structs so that concurrent threads
 * can use jpeglib error handling.
 */

struct oil_decompress {
    struct jpeg_decompress_struct pub;
    jmp_buf jmp;
};

struct oil_compress {
    struct jpeg_compress_struct pub;
    jmp_buf jmp;
    unsigned char *in_buf;
};

/* jpeglib error handling */

struct jpeg_error_mgr jerr;

static void
error_exit(j_common_ptr info)
{
    struct oil_decompress *dinfo;
    struct oil_compress *cinfo;

    dinfo = (struct oil_decompress *)info;
    cinfo = (struct oil_compress *)info;
    longjmp(info->is_decompressor ? dinfo->jmp : cinfo->jmp, 1);
}

static void output_message(j_common_ptr info) {}

/* jpeglib input callbacks */

struct jpeg_src {
    struct jpeg_source_mgr mgr;
    unsigned char *buf;
    read_fn_t read;
    void *ctx;
};

/* EOF marker that is given to jpeglib when the IO source prematurely ends */
static unsigned char jpeg_eof[2];
static unsigned char jpeg_sig[3];

static boolean
fill_input_buffer(j_decompress_ptr cinfo)
{
    struct jpeg_src *src;
    size_t out_len;
    int ret;

    src = (struct jpeg_src *)cinfo->src;
    ret = src->read(src->ctx, BUFSIZE, src->buf, &out_len);

    if (ret) {
	strcpy(cinfo->err->msg_parm.s, "IO Failed");
	cinfo->err->error_exit((j_common_ptr)cinfo);
    } else if (!out_len) {
	// TODO: add warning that input ended prematurely
	src->mgr.next_input_byte = jpeg_eof;
	src->mgr.bytes_in_buffer = 2;
    } else {
	src->mgr.bytes_in_buffer = out_len;
	src->mgr.next_input_byte = src->buf;
    }

    return TRUE;
}

static void
skip_input_data(j_decompress_ptr cinfo, long num_bytes)
{
    struct jpeg_source_mgr * src;

    src = cinfo->src;
    while (num_bytes > (long)src->bytes_in_buffer) {
	num_bytes -= src->bytes_in_buffer;
	fill_input_buffer(cinfo);
    }
    src->next_input_byte += num_bytes;
    src->bytes_in_buffer -= num_bytes;
}

static void term_source(j_decompress_ptr cinfo) {}
static void init_source(j_decompress_ptr cinfo) {}

/* jpeglib output callbacks */

struct jpeg_dest {
    struct jpeg_destination_mgr mgr;
    unsigned char *buf;
    write_fn_t write;
    void *ctx;
};

static void init_destination(j_compress_ptr cinfo) {}

static boolean
empty_output_buffer(j_compress_ptr cinfo)
{
    struct jpeg_dest *dest;

    dest = (struct jpeg_dest *)cinfo->dest;
    if (dest->write(dest->ctx, dest->buf, BUFSIZE))
    	cinfo->err->error_exit((j_common_ptr)cinfo);
    dest->mgr.next_output_byte = (JOCTET *)dest->buf;
    dest->mgr.free_in_buffer = BUFSIZE;
    return TRUE;
}

static void
term_destination(j_compress_ptr cinfo)
{
    struct jpeg_dest *dest;
    size_t len;

    dest = (struct jpeg_dest *)cinfo->dest;
    len = BUFSIZE - dest->mgr.free_in_buffer;

    if (len)
	if (dest->write(dest->ctx, dest->buf, len))
	    cinfo->err->error_exit((j_common_ptr)cinfo);
}

/* oil image functions */

static int
jpeg_get_scanline(struct image *image, unsigned char *sl)
{
    j_decompress_ptr dinfo;
    struct oil_decompress *oil_dinfo;

    dinfo = (j_decompress_ptr)image->data;
    oil_dinfo = (struct oil_decompress *)dinfo;

    if (setjmp(oil_dinfo->jmp))
	return -1;

    if (!dinfo->output_scanline)
	jpeg_start_decompress(dinfo);

    jpeg_read_scanlines(dinfo, (JSAMPROW *)&sl, 1);
    return 0;
}

static void
jpeg_free(struct image *image)
{
    j_decompress_ptr dinfo;
    struct jpeg_src *src;

    dinfo = (j_decompress_ptr)image->data;
    src = (struct jpeg_src *)dinfo->src;
    jpeg_abort_decompress(dinfo);
    free(src->buf);
    free(src);

    jpeg_destroy_decompress(dinfo);
    free(dinfo);
}

int
jpeg_init(struct image *image, read_fn_t read, void *ctx, int sig_bytes)
{
    struct jpeg_src *src;
    j_decompress_ptr dinfo;
    struct oil_decompress *oil_dinfo;

    image->free = jpeg_free;
    image->icc_marker = NULL;

    dinfo = image->data = malloc(sizeof(struct oil_decompress));
    oil_dinfo = (struct oil_decompress *)dinfo;
    dinfo->err = &jerr;
    jpeg_create_decompress(dinfo);

    src = malloc(sizeof(struct jpeg_src));
    memset(src, 0, sizeof(struct jpeg_src));
    src->buf = malloc(BUFSIZE);

    dinfo->src = (struct jpeg_source_mgr *)src;
    src->read = read;
    src->ctx = ctx;
    src->mgr.init_source = init_source;
    src->mgr.fill_input_buffer = fill_input_buffer;
    src->mgr.skip_input_data = skip_input_data;
    src->mgr.resync_to_restart = jpeg_resync_to_restart;
    src->mgr.term_source = term_source;

    if (sig_bytes > 0 && sig_bytes < 4) {
        src->mgr.next_input_byte = jpeg_sig;
        src->mgr.bytes_in_buffer = sig_bytes;
    } else if (sig_bytes) {
       return -1;
    }

    if (setjmp(oil_dinfo->jmp))
        return -1;

    // Keep ICC Marker
    // http://jgge.googlecode.com/svn-history/r19/trunk/Libraries/cegui_mk2/dependencies/include/FreeImage/PluginJPEG.cpp
    jpeg_save_markers(dinfo, ICC_MARKER, 0xFFFF);

    jpeg_read_header(dinfo, TRUE);
    
    jpeg_calc_output_dimensions(dinfo);

    image->icc_marker = NULL;
    if(dinfo->marker_list) {
        image->icc_marker = dinfo->marker_list;
    }

    image->get_scanline = jpeg_get_scanline;
    image->width = dinfo->output_width;
    image->height = dinfo->output_height;
    image->cmp = dinfo->output_components;

    return 0;
}

void
jpeg_set_scale_denom(struct image *i, int denom)
{
    j_decompress_ptr dinfo;

    dinfo = (j_decompress_ptr)i->data;
    dinfo->scale_denom = denom;
    jpeg_calc_output_dimensions(dinfo);
    i->width = dinfo->output_width;
    i->height = dinfo->output_height;
}

/* writer */

static void
jpeg_writer_free(struct writer *writer)
{
    j_compress_ptr cinfo;
    struct oil_compress *cinfo_oil;
    struct jpeg_dest *dest;

    cinfo = (j_compress_ptr)writer->data;
    dest = (struct jpeg_dest *)cinfo->dest;
    cinfo_oil = (struct oil_compress *)cinfo;

    jpeg_destroy_compress(cinfo);
    free(dest->buf);
    free(dest);
    free(cinfo_oil->in_buf);
    free(cinfo);
}

static int
jpeg_writer_write(struct writer *writer)
{
    j_compress_ptr cinfo;
    struct oil_compress *oil_cinfo;
    struct image *src;
    unsigned char *buf;
    long i;
    jpeg_saved_marker_ptr icc;

    src = writer->src;
    cinfo = (j_compress_ptr)writer->data;
    oil_cinfo = (struct oil_compress *)writer->data;
    buf = oil_cinfo->in_buf;

    if (setjmp(oil_cinfo->jmp))
        return -1;

    jpeg_start_compress(cinfo, TRUE);

    icc = src->icc_marker;
    if(icc != NULL) {
        jpeg_write_marker(cinfo, ICC_MARKER, icc->data , 
            icc->original_length);

    }

    for (i=0; i<src->height; i++) {
	if (src->get_scanline(src, buf))
	    return -1;
	jpeg_write_scanlines(cinfo, (JSAMPROW *)&buf, 1);
    }

    jpeg_finish_compress(cinfo);

    return 0;
}

void
jpeg_writer_init(struct writer *writer, write_fn_t write_fn, void *ctx,
		 struct image *src, int progressive, int quality)
{
    j_compress_ptr cinfo;
    struct oil_compress *oil_cinfo;
    struct jpeg_dest *dest;

    dest = malloc(sizeof(struct jpeg_dest));
    memset(dest, 0, sizeof(struct jpeg_dest));
    dest->buf = malloc(BUFSIZE);
    dest->ctx = ctx;
    dest->write = write_fn;
    dest->mgr.next_output_byte = dest->buf;
    dest->mgr.free_in_buffer = BUFSIZE;
    dest->mgr.init_destination = init_destination;
    dest->mgr.empty_output_buffer = empty_output_buffer;
    dest->mgr.term_destination = term_destination;

    cinfo = malloc(sizeof(struct oil_compress));
    cinfo->err = &jerr;
    jpeg_create_compress(cinfo);
    cinfo->dest = (struct jpeg_destination_mgr *)dest;
    cinfo->image_width = src->width;
    cinfo->image_height = src->height;
    cinfo->input_components = src->cmp;
    // TODO: Fail if alpha
    cinfo->in_color_space = src->cmp == 3 ? JCS_RGB : JCS_GRAYSCALE;

    oil_cinfo = (struct oil_compress *)cinfo;
    oil_cinfo->in_buf = malloc(src->width * src->cmp);

    jpeg_set_defaults(cinfo);

    // The function jpeg_set_quality() shall construct JPEG quantization tables 
    // for the given quality setting. The quality value ranges from 0..100. 
    // If "force_baseline" is TRUE, the computed quantization table entries are limited to 1..255 for JPEG baseline compatibility.
    jpeg_set_quality(cinfo, quality, 1);
    
    if(progressive) {
        jpeg_simple_progression(cinfo);
    }

    writer->src = src;
    writer->data = cinfo;
    writer->write = jpeg_writer_write;
    writer->free = jpeg_writer_free;
}

/* initialization
 *
 * TODO: lazy initialize on jpeg_init and get rid of jpeg_appinit(). beware of
 * thread safety
 */

void
jpeg_appinit() {
    jpeg_eof[0] = 0xFF;
    jpeg_eof[1] = JPEG_EOI;
    jpeg_sig[0] = 0xFF;
    jpeg_sig[1] = 0xD8;
    jpeg_sig[2] = 0xFF;
    jpeg_std_error(&jerr);
    jerr.error_exit = error_exit;
    jerr.output_message = output_message;
}
