#include <ruby.h>

void Init_jpeg();
void Init_png();

void Init_oil()
{
   Init_jpeg();
   Init_png();
}
