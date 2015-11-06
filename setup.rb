require 'net/http'
require 'fileutils'
require 'uri'

begin
  unless require 'zip'
    raise "NO ZIP DUDE"
  end
rescue LoadError
  `gem install zip --no-rdoc --no-ri`
  unless require 'zip'
    raise "NO ZIP DUDE"
  end
end

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
        end
      end
    end
  end
end

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
