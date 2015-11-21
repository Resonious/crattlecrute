require 'net/http'
require 'fileutils'
require 'uri'
require 'openssl'

# Setup garbage
# NOTE this stuff doesn't actually work properly lol...

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

destination_path = File.dirname File.expand_path __FILE__

def download(uri, dest, start_msg="Downloading")
  STDOUT.write start_msg

  http_req = Net::HTTP.new(uri.host, uri.port)
  http_req.use_ssl = true
  if OS.windows?
    http_req.ca_file = File.join((File.dirname File.expand_path __FILE__), 'win-cacert.pem')
  end
  http_req.verify_mode = OpenSSL::SSL::VERIFY_PEER
  http_req.verify_depth = 5

  http_req.start do |http|
    request = Net::HTTP::Get.new uri.request_uri
    http.read_timeout = 500
    http.request request do |response|
      File.open(dest, 'wb') do |local_file|
        response.read_body do |chunk|
          local_file.write(chunk)
          putc '.'
        end
        puts '.'
      end
    end
  end
end

# Grab STB libs
FileUtils.mkdir_p File.join(destination_path, "STB")
download(
  URI("https://raw.githubusercontent.com/nothings/stb/master/stb_image.h"),
  File.join(destination_path, "STB", "stb_image.h"),
  "Downloading stb_image"
)
download(
  URI("https://raw.githubusercontent.com/nothings/stb/master/stb_vorbis.c"),
  File.join(destination_path, "STB", "stb_vorbis.c"),
  "Downloading stb_vorbis"
)
download(
  URI("https://raw.githubusercontent.com/nothings/stb/master/stb_truetype.h"),
  File.join(destination_path, "STB", "stb_truetype.h"),
  "Downloading stb_truetype"
)

# Actually download SDL
begin
  download(
    URI("https://www.libsdl.org/release/SDL2-2.0.3.zip"),
    "SDL2temp.zip",
    "Downloading SDL2"
  )
  puts "Done! Now unzipping."

  ZipFile = (Zip::ZipFile rescue Zip::File)
  ZipFile.open("SDL2temp.zip") do |zipped_files|
    zipped_files.each do |zip_file|
      path = File.join(destination_path, zip_file.name.gsub('SDL2-2.0.3', 'SDL'))
      FileUtils.mkdir_p File.dirname(path)

      if File.exists?(path)
        puts "Skipping #{path} - already exists."
      else
        zipped_files.extract(zip_file.to_s, path) 
      end
    end
  end

  puts "And done."

  if OS.windows?
    Shortcut.new('VS.lnk') do |s|
      s.target_path = File.join destination_path, 'SDL\VisualC\SDL_VS2013.sln'
      s.description = 'Shortcut to Visual Studio solution'
    end
    puts "Added a shortcut to the Visual Studio solution. That's all for now!"
  end

ensure
  FileUtils.rm "SDL2temp.zip"
end
