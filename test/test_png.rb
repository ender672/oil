require 'minitest'
require 'minitest/autorun'
require 'oil'
require 'stringio'
require 'helper'

class TestPNG < MiniTest::Test
  # http://garethrees.org/2007/11/14/pngcrush/
  PNG_DATA = "\
\x89\x50\x4E\x47\x0D\x0A\x1A\x0A\x00\x00\x00\x0D\x49\x48\x44\x52\x00\x00\x00\
\x01\x00\x00\x00\x01\x01\x00\x00\x00\x00\x37\x6E\xF9\x24\x00\x00\x00\x10\x49\
\x44\x41\x54\x78\x9C\x62\x60\x01\x00\x00\x00\xFF\xFF\x03\x00\x00\x06\x00\x05\
\x57\xBF\xAB\xD4\x00\x00\x00\x00\x49\x45\x4E\x44\xAE\x42\x60\x82".b

  BIG_PNG = begin
    s = ""
    r = Oil::PNGReader.new(StringIO.new(PNG_DATA))
    r.scale_width = 500
    r.scale_height = 1000
    r.each{ |a| s << a }
    s
  end

  def test_valid
    o = Oil::PNGReader.new(png_io)
    assert_equal 1, o.width
    assert_equal 1, o.height
  end

  def test_bogus_header_chunk
    str = PNG_DATA.dup
    str[15] = "\x10"
    assert_raises(RuntimeError) { drain_string(str) }
  end

  def test_bogus_body_chunk
    str = PNG_DATA.dup
    str[37] = "\x10"
    assert_raises(RuntimeError) { drain_string(str) }
  end

  def test_bogus_end_chunk
    str = PNG_DATA.dup
    str[-6] = "\x10"
    io = StringIO.new(str)
    o = Oil::PNGReader.new(png_io)
    assert_equal 1, o.width
    assert_equal 1, o.height
  end

  # Allocation tests

  def test_multiple_initialize_leak
    o = Oil::PNGReader.allocate

    o.send(:initialize, png_io)
    o.each{ |d| }

    o.send(:initialize, png_io)
    o.each{ |d| }
  end

  # Test io

  IO_OFFSETS = [0, 10, 20]#, 8191, 8192, 8193, 12000]

  def iotest(io_class)
    IO_OFFSETS.each do |i|
      yield io_class.new(BIG_PNG, :byte_count => i)
    end
  end

  def resize(io)
    Oil::PNGReader.new(io).each{ |d| }
  end

  def test_io_too_much_data
    iotest(GrowIO) do |io|
      assert_raises(RuntimeError) { resize(io) }
    end
  end

  def test_io_does_nothing
    iotest(NilIO) do |io|
      assert_raises(TypeError) { resize(io) }
    end
  end

  def test_io_raises_exception
    iotest(RaiseIO) do |io|
      assert_raises(CustomError) { resize(io) }
    end
  end

  def test_io_throws
    iotest(ThrowIO) do |io|
      assert_throws(:foo) { resize(io) }
    end
  end

  def test_io_shrinks_buffer
    iotest(ShrinkIO) do |io|
      assert_raises(RuntimeError) { resize(io) }
    end
  end

  def test_not_string_io
    iotest(NotStringIO) do |io|
      assert_raises(TypeError) { resize(io) }
    end
  end

  # Test yielding

  def test_raise_in_each
    assert_raises(CustomError) do
      Oil::PNGReader.new(png_io).each { raise CustomError }
    end
  end

  def test_throw_in_each
    catch(:foo) do
      Oil::PNGReader.new(png_io).each { throw :foo }
    end
  end

  def test_each_in_each
    o = Oil::PNGReader.new(png_io)
    o.each do |d|
      assert_raises(RuntimeError){ o.each { |e| } }
    end
  end

  def test_each_shrinks_buffer
    Oil::PNGReader.new(png_io).each { |d| d.slice!(0, 4) }
  end
  
  def test_each_enlarges_buffer
    Oil::PNGReader.new(png_io).each { |d| d << "foobar" }
  end

  private

  def png_io
    StringIO.new(PNG_DATA)
  end

  def drain_string(str)
    Oil::PNGReader.new(StringIO.new(str)).each{|s|}
  end
end
