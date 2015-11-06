require 'net/http'
require 'fileutils'
require 'uri'

# Setup garbage

begin
  require 'zip'
rescue LoadError
  system 'gem install zip --no-rdoc --no-ri'
  require 'zip'
end

begin
  require 'os'
rescue LoadError
  system 'gem install os --no-rdoc --no-ri'
  require 'os'
end

if OS.windows?
  begin
    require 'win32/shortcut'
  rescue LoadError
    system 'gem install win32-shortcut --no-rdoc --no-ri'
    require 'win32/shortcut'
  end
  include Win32
end

# Actually download SDL

STDOUT.write "Downloading SDL"
uri = URI("http://www.libsdl.org/release/SDL2-2.0.3.zip")
http_req = Net::HTTP.new(uri.host, uri.port)
http_req.use_ssl = false
begin
  http_req.start do |http|
    request = Net::HTTP::Get.new uri.request_uri
    http.read_timeout = 500
    http.request request do |response|
      File.open("SDL2temp.zip", 'wb') do |local_file|
        response.read_body do |chunk|
          local_file.write(chunk)
          putc '.'
        end
        puts '.'
      end
    end
  end
end
puts "Done! Now unzipping."

destination_path = File.dirname File.expand_path __FILE__

Zip::ZipFile.open("SDL2temp.zip") do |zipped_files|
  zipped_files.each do |zip_file|
    path = File.join(destination_path, zip_file.name.gsub!('SDL2-2.0.3', 'SDL'))
    FileUtils.mkdir_p File.dirname(path)

    if File.exists?(path)
      puts "Skipping #{path} - already exists."
    else
      zipped_files.extract(zip_file, path) 
    end
  end
end

FileUtils.rm "SDL2temp.zip"

puts "And done."

if OS.windows?
  Shortcut.new('VS.lnk') do |s|
    s.target_path = File.join destination_path, 'SDL\VisualC\SDL_VS2013.sln'
    s.description = 'Shortcut to Visual Studio solution'
  end
  puts "Added a shortcut to the Visual Studio solution. That's all for now!"
end
