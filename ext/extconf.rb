require 'mkmf'

$LOCAL_LIBS = "liboil/liboil.a"

dir_config('jpeg')
dir_config('png')
find_header('oil.h', path="liboil")

unless have_header('jpeglib.h')
  abort "libjpeg headers were not found."
end

unless have_library('jpeg')
  abort "libjpeg was not found."
end

unless have_header('png.h')
  abort "libpng headers were not found."
end

unless have_library('png')
  abort "libpng was not found."
end

create_makefile('oil')
