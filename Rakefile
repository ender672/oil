require 'rake/extensiontask'
require 'rubygems/package_task'
require 'rake/testtask'

Rake::ExtensionTask.new('oil') do |ext|
  ext.lib_dir = 'lib/oil'
end

s = Gem::Specification.load('oil.gemspec')

Gem::PackageTask.new(s){}

Rake::TestTask.new do |t|
  t.libs = ['lib', 'test']
  t.test_files = FileList['test/test_jpeg.rb', 'test/test_png.rb']
end

task test: :compile
task default: :test
