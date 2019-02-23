module Oil
  VERSION = "0.2.1"

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

  def self.new_jpeg_reader(io, box_width, box_height)
    o = JPEGReader.new(io, [:COM, :APP1, :APP2])

    # bump RGB images to RGBX
    if (o.out_color_space == :RGB)
      o.out_color_space = :RGBX
    end

    # JPEG Pre-scaling is equivalent to a box filter at an integer scale factor.
    destw, desth = Oil.fix_ratio(o.output_width, o.output_height, box_width, box_height)
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
