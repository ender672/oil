require 'rake/clean'
require 'rake/testtask'

file 'ext/oil.jar' => FileList['ext/*.java'] do
  cd 'ext' do
    sh "javac -g -cp #{Config::CONFIG['prefix']}/lib/jruby.jar #{FileList['*.java']}"
    quoted_files = (FileList.new('*.class').to_a.map { |f| "'#{f}'" }).join(' ')
    sh "jar cf oil.jar #{quoted_files}"
  end
end

file 'ext/Makefile' do
  cd 'ext' do
    ruby "extconf.rb #{ENV['EXTOPTS']}"
  end
end

file 'ext/liboil/liboil.a' => FileList.new('ext/liboil/oil.c',
  'ext/liboil/jpeg.c', 'ext/liboil/png.c', 'ext/liboil/resample.c') do
  cd 'ext/liboil' do
    sh 'make CFLAGS="-Ofast -march=native -fPIC"'
  end
end

file 'ext/oil.so' => FileList.new('ext/Makefile', 'ext/oilrb.c',
  'ext/liboil/liboil.a') do
  cd 'ext' do
    sh 'make'
  end
end

Rake::TestTask.new do |t|
  t.libs = ['ext', 'test']
  t.test_files = FileList['test/test_*.rb']
end

CLEAN.add('ext/*{.o,.so,.log,.class,.jar}', 'ext/Makefile')
CLOBBER.add('*.gem')

desc 'Build the gem and include the java library'
task :gem => "ext/oil.jar" do
  system "gem build oil.gemspec"
end

desc 'Compile the extension'
task :compile => "ext/oil.#{RUBY_PLATFORM =~ /java/ ? 'jar' : 'so'}"

task :test => :compile
task :default => :test
