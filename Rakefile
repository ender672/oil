require 'rake/extensiontask'
require 'rubygems/package_task'
require 'rake/testtask'

Rake::ExtensionTask.new('oil') do |ext|
  ext.lib_dir = 'lib/oil'
end

s = Gem::Specification.new('oil', '0.2.0') do |s|
  s.license = 'MIT'
  s.summary = 'Resize JPEG and PNG images.'
  s.description = 'Resize JPEG and PNG images, aiming for fast performance and low memory use.'
  s.authors = ['Timothy Elliott']
  s.email = 'tle@holymonkey.com'
  s.files = %w{
    Rakefile
    README.rdoc
    MIT-LICENSE
    lib/oil.rb
    ext/oil/resample.c
    ext/oil/resample.h
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

Gem::PackageTask.new(s){}

Rake::TestTask.new do |t|
  t.libs = ['lib', 'test']
  t.test_files = FileList['test/test_jpeg.rb', 'test/test_png.rb']
end

task test: :compile
task default: :test
