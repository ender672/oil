#include <ruby.h>
#include "oil_resample.h"

static VALUE rb_fix_ratio(VALUE self, VALUE src_w, VALUE src_h, VALUE out_w, VALUE out_h)
{
	int out_width, out_height;
	VALUE ret;

	out_width = NUM2INT(out_w);
	out_height = NUM2INT(out_h);
	oil_fix_ratio(NUM2INT(src_w), NUM2INT(src_h), &out_width, &out_height);
	ret = rb_ary_new2(2);
	rb_ary_push(ret, INT2FIX(out_width));
	rb_ary_push(ret, INT2FIX(out_height));
	return ret;
}

void Init_jpeg();
void Init_png();

void Init_oil()
{
	VALUE mOil;
	mOil = rb_const_get(rb_cObject, rb_intern("Oil"));
	rb_define_singleton_method(mOil, "fix_ratio", rb_fix_ratio, 4);
	Init_jpeg();
	Init_png();
}
