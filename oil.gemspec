Gem::Specification.new do |s|
  s.name = 'oil'
  s.version = '0.3.0'
  s.license = 'MIT'
  s.required_ruby_version = '>= 2.7'
  s.summary = 'Resize JPEG and PNG images.'
  s.description = 'Resize JPEG and PNG images, aiming for fast performance and low memory use.'
  s.authors = ['Timothy Elliott']
  s.email = 'tle@holymonkey.com'
  s.files = %w{
    Rakefile
    README.rdoc
    MIT-LICENSE
    oil.gemspec
    lib/oil.rb
    ext/oil/oil_resample.c
    ext/oil/oil_resample.h
    ext/oil/oil_resample_internal.h
    ext/oil/oil_resample_sse2.c
    ext/oil/oil_libjpeg.c
    ext/oil/oil_libjpeg.h
    ext/oil/oil_libpng.c
    ext/oil/oil_libpng.h
    ext/oil/jpeg.c
    ext/oil/png.c
    ext/oil/oil.c
    ext/oil/extconf.rb
    test/helper.rb
    test/test_jpeg.rb
    test/test_png.rb
  }
  s.homepage = 'http://github.com/ender672/oil'
  s.extensions << 'ext/oil/extconf.rb'
  s.extra_rdoc_files = ['README.rdoc']
end
