require 'minitest'
require 'minitest/autorun'
require 'oil'
require 'stringio'
require 'helper'

class TestJPEG < MiniTest::Test
  # http://stackoverflow.com/a/2349470
  JPEG_DATA = "\
\xff\xd8\xff\xe0\x00\x10\x4a\x46\x49\x46\x00\x01\x01\x01\x00\x48\x00\x48\x00\
\x00\xff\xdb\x00\x43\x00\x03\x02\x02\x02\x02\x02\x03\x02\x02\x02\x03\x03\x03\
\x03\x04\x06\x04\x04\x04\x04\x04\x08\x06\x06\x05\x06\x09\x08\x0a\x0a\x09\x08\
\x09\x09\x0a\x0c\x0f\x0c\x0a\x0b\x0e\x0b\x09\x09\x0d\x11\x0d\x0e\x0f\x10\x10\
\x11\x10\x0a\x0c\x12\x13\x12\x10\x13\x0f\x10\x10\x10\xff\xc9\x00\x0b\x08\x00\
\x01\x00\x01\x01\x01\x11\x00\xff\xcc\x00\x06\x00\x10\x10\x05\xff\xda\x00\x08\
\x01\x01\x00\x00\x3f\x00\xd2\xcf\x20\xff\xd9".b

  BIG_JPEG = begin
    s = ""
    r = Oil::JPEGReader.new(StringIO.new(JPEG_DATA))
    r.scale_width = 2000
    r.scale_height = 2000
    r.each{ |a| s << a }
    s
  end

  def test_valid
    o = Oil::JPEGReader.new(jpeg_io)
    assert_equal 1, o.image_width
    assert_equal 1, o.image_height
  end

  def test_missing_eof
    io = StringIO.new(JPEG_DATA[0..-2])
    o = Oil::JPEGReader.new(io)
    assert_equal 1, o.image_width
    assert_equal 1, o.image_height
  end

  def test_bogus_header_marker
    str = JPEG_DATA.dup
    str[3] = "\x10"
    assert_raises(RuntimeError) { drain_string(str) }
  end

  def test_bogus_body_marker
    str = JPEG_DATA.dup
    str[-22] = "\x10"
    assert_raises(RuntimeError) { drain_string(str) }
  end

  def test_color_space
    o = Oil::JPEGReader.new(jpeg_io)
    assert_equal :GRAYSCALE, o.jpeg_color_space
    assert_equal :GRAYSCALE, o.out_color_space
    assert_equal 1, o.num_components
    assert_equal 1, o.output_components
    assert_equal 1, o.out_color_components
  end

  # Allocation tests

  def test_multiple_initialize_leak
    o = Oil::JPEGReader.allocate

    o.send(:initialize, jpeg_io)
    o.each{ |d| }

    o.send(:initialize, jpeg_io)
    o.each{ |d| }
  end

  # Test io

  IO_OFFSETS = [0, 10, 20, 1023, 1024, 1025, 8191, 8192, 8193, 12000]

  def iotest(io_class)
    IO_OFFSETS.each do |i|
      yield io_class.new(BIG_JPEG, :byte_count => i)
    end
  end

  def resize(io)
    o = Oil::JPEGReader.new(io)
    o.scale_width = 10
    o.scale_height = 20
    o.each{ |d| }
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

  # This causes valgrind errors, but I'm pretty sure that the libjpeg is
  # returning uninitialized memory because the source jpeg is corrupt.
  def test_io_shrinks_buffer
    iotest(ShrinkIO) do |io|
      resize(io) rescue nil
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
      Oil::JPEGReader.new(jpeg_io).each{ raise CustomError }
    end
  end

  def test_throw_in_each
    catch(:foo) do
      Oil::JPEGReader.new(jpeg_io).each{ throw :foo }
    end
  end

  def test_each_in_each
    o = Oil::JPEGReader.new(jpeg_io)
    o.each do |d|
      assert_raises(RuntimeError) do
        o.each { |e| }
      end
    end
  end

  def test_each_shrinks_buffer
    Oil::JPEGReader.new(jpeg_io).each{ |d| d.slice!(0, 4) }
  end
  
  def test_each_enlarges_buffer
    Oil::JPEGReader.new(jpeg_io).each{ |d| d << "foobar" }
  end

  def test_marker_roundtrip
    str = ""
    opts = { markers: { COM: ["hello world", "foobar123"]}}
    Oil::JPEGReader.new(jpeg_io).each(opts){ |s| str << s }

    r = Oil::JPEGReader.new(StringIO.new(str), [:COM])

    assert_equal(r.markers, opts[:markers])
  end

  def test_marker_code_unrecognized
    assert_raises(RuntimeError) do
      Oil::JPEGReader.new(jpeg_io, [:FOOBAR])
    end
  end

  def test_marker_codes_not_array
    assert_raises(TypeError) do
      Oil::JPEGReader.new(jpeg_io, 1234)
    end
  end

  def test_marker_code_not_symbol
    assert_raises(TypeError) do
      Oil::JPEGReader.new(jpeg_io, [1234])
    end
  end

  def test_marker_too_big
    opts = { markers: { COM: ["hello world"*10000]}}

    assert_raises(RuntimeError) do
      Oil::JPEGReader.new(jpeg_io).each(opts){ |s| str << s }
    end
  end

  def test_markers_not_hash
    opts = { markers: 1234 }

    assert_raises(TypeError) do
      Oil::JPEGReader.new(jpeg_io).each(opts){}
    end
  end

  def test_marker_value_not_array
    opts = { markers: { COM: 1234}}

    assert_raises(TypeError) do
      Oil::JPEGReader.new(jpeg_io).each(opts){}
    end
  end

  def test_marker_value_entry_not_string
    opts = { markers: { COM: [1234]}}

    assert_raises(TypeError) do
      Oil::JPEGReader.new(jpeg_io).each(opts){}
    end
  end

  def test_quality_not_a_number
    opts = { quality: "foobar" }
    assert_raises(TypeError) do
      Oil::JPEGReader.new(jpeg_io).each(opts){}
    end
  end

  private

  def jpeg_io
    StringIO.new(JPEG_DATA)
  end

  def drain_string(str)
    Oil::JPEGReader.new(StringIO.new(str)).each{ |s| }
  end
end
