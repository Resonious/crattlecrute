require 'fileutils'

assets_base_dir = File.dirname File.expand_path __FILE__

FileUtils.mkdir_p "#{assets_base_dir}/build"

# Open files for the .h and the assets themselves.
assets = File.new("#{assets_base_dir}/build/crattlecrute.assets", 'wb')
header = File.new("#{assets_base_dir}/src/assets.h", 'w')

header.write(%(#include "types.h"\n))
header.write(%(#include "SDL.h"\n))
header.write("// This is generated on compile - don't change it by hand!\n")
header.write("// Generated #{Time.now}\n\n")

all_files = Dir.glob("#{assets_base_dir}/assets/**/*").reject(&File.method(:directory?))

header.write(
  "typedef struct { byte* bytes; long long size; } AssetFile;\n"\
  "int open_assets_file();\n"\
  "SDL_Surface* load_image(int asset);\n"\
  "SDL_Texture* load_texture(SDL_Renderer* renderer, int asset);\n"\
  "void free_image(SDL_Surface* image);\n"\
  "AssetFile load_asset(int asset);\n\n"
)

header.write(
  "const static struct {\n"\
  "    long long offset, size;\n"\
  "} ASSETS[#{all_files.size * 2}] = {\n"
)

def ident(file)
  file.gsub(/[\.\s\?!\/\\-]/, '_').upcase
end

current_offset = 0
# Write the start of the header and the assets.
all_files.each do |file|
  bytes = IO.binread(file)
  assets.write(bytes)

  puts file.gsub! /^.*[\/\\]assets[\/\\]/, ''
  header.write(
    "    // #{file}\n"\
    "    #{current_offset}, #{bytes.size},\n"\
  )

  current_offset += bytes.size
end

header.write "};\n\n"

all_files.each_with_index do |file, index|
  header.write("const static int ASSET_#{ident(file)} = #{index};\n")
end

assets.close
header.close
