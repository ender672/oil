module Oil
  VERSION = "0.1.0"

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

  # JPEG Pre-scaling is equivalent to a box filter at an integer scale factor.
  # We don't use this to scale down past 4x the target image size in order to
  # get proper bicubic scaling in the final image.
  def self.pre_scale(sw, sh, dw, dh)
    inv_scale = sw / dw
    inv_scale /= 4

    if inv_scale >= 8
      return 8
    elsif inv_scale >= 4
      return 4
    elsif inv_scale >= 2
      return 2
    else
      return 0
    end
  end

  def self.new(io, box_width, box_height)
    a = io.getc
    b = io.getc
    io.ungetc(b)
    io.ungetc(a)

    if (a == "\xFF".b && b == "\xD8".b)
      o = JPEGReader.new(io, [:COM, :APP1, :APP2])
      if (o.color_space == :RGB)
        o.out_color_space = :RGBX
      end
    elsif (a == "\x89".b && b == "P".b)
      o = PNGReader.new(io)
    else
      raise "Unknown image file format."
    end

    destw, desth = self.fix_ratio(o.width, o.height, box_width, box_height)
    pre = self.pre_scale(o.width, o.height, box_width, box_height)

    o.scale_width = destw
    o.scale_height = desth

    if o.respond_to?(:scale_denom) and pre > 0
      o.scale_denom = pre
    end

    return o
  end
end

require 'oil/oil.so'
