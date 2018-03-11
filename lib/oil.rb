module Oil
  VERSION = "0.1.3"

  def self.sniff_signature(io)
    a = io.getc
    b = io.getc
    io.ungetc(b)
    io.ungetc(a)

    if (a == "\xFF".b && b == "\xD8".b)
      return :JPEG
    elsif (a == "\x89".b && b == "P".b)
      return :PNG
    end
  end

  def self.new(io, box_width, box_height)
    case sniff_signature(io)
    when :JPEG
      return new_jpeg_reader(io, box_width, box_height)
    when :PNG
      return new_png_reader(io, box_width, box_height)
    else
      raise "Unknown image file format."
    end
  end

  private

  def self.fix_ratio(sw, sh, boxw, boxh)
    x = boxw / sw.to_f
    y = boxh / sh.to_f

    destw = boxw
    desth = boxh

    if x < y
      desth = (sh * x).round
    else
      destw = (sw * y).round
    end

    if desth < 1
      desth = 1
    end

    if destw < 1
      destw = 1
    end

    return destw, desth
  end

  def self.cubic_scale_ratio(src_dim, out_dim)
    tmp = out_dim * 8 * 3 / (src_dim * 2)
    rest = out_dim * 8 * 3 % (src_dim * 2)
    if (rest)
      tmp += 1
    end
    if (tmp < 8)
      return tmp, 8
    end
    return 1, 1
  end

  def self.new_jpeg_reader(io, box_width, box_height)
    o = JPEGReader.new(io, [:COM, :APP1, :APP2])

    # bump RGB images to RGBX
    if (o.out_color_space == :RGB)
      o.out_color_space = :RGBX
    end

    # JPEG Pre-scaling is equivalent to a box filter at an integer scale factor.
    # We don't use this to scale down past 4x the target image size in order to
    # get proper bicubic scaling in the final image.
    scale_num, scale_denom = self.cubic_scale_ratio(o.image_width, box_width)
    o.scale_num = scale_num
    o.scale_denom = scale_denom

    destw, desth = self.fix_ratio(o.output_width, o.output_height, box_width, box_height)
    o.scale_width = destw
    o.scale_height = desth

    return JPEGReaderWrapper.new(o, { markers: o.markers, quality: 95 })
  end

  def self.new_png_reader(io, box_width, box_height)
    o = PNGReader.new(io)
    destw, desth = self.fix_ratio(o.width, o.height, box_width, box_height)
    o.scale_width = destw
    o.scale_height = desth
    return o
  end
end

class JPEGReaderWrapper
  def initialize(reader, opts)
    @reader = reader
    @opts = opts
  end

  def each(&block)
    @reader.each(@opts, &block)
  end
end

require 'oil/oil.so'
