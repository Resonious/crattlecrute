require 'fileutils'
require 'chunky_png'
require 'nokogiri'
require_relative 'tilemap_compressor'

assets_base_dir = File.dirname File.expand_path __FILE__

FileUtils.mkdir_p "#{assets_base_dir}/build"

# First compile all .tmx files in asset-dev to compressed form
puts "finding .tmx files in #{assets_base_dir}/asset-dev/maps"
Dir.glob("#{assets_base_dir}/asset-dev/maps/*.tmx").reject(&File.method(:directory?)).each do |map_tmx_file|
  out_file = map_tmx_file
    .gsub('asset-dev', 'assets')
    .gsub('.tmx', '.cm')
  puts "COMPILING #{map_tmx_file} TO #{out_file}"

  begin
    FileUtils.mkdir_p(File.dirname(out_file))
    write_cm(read_tmx(map_tmx_file), out_file)
  rescue StandardError => e
    puts "===================================="
    puts "ERROR COMPILING TILEMAP #{map_tmx_file}\n#{e}"
    puts "===================================="
  end
end


# Open files for the .h and the assets themselves.
assets = File.new("#{assets_base_dir}/build/crattlecrute.assets", 'wb')
header = File.new("#{assets_base_dir}/src/assets.h", 'w')

header.write("#ifndef ASSETS_H\n")
header.write("#define ASSETS_H\n\n")
header.write(%(#include "types.h"\n))
header.write(%(#include "tilemap.h"\n))
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

current_offset = 0
# Write the start of the header and the assets.
CollisionHeights = Struct.new(:top2down, :bottom2up, :left2right, :right2left)
all_collision_data = {}
maps = []
to_remove = []
all_files.each do |file|
  # Handle .collision.png files specially
  if /^(?<for_file>.+)\.collision.png$/ =~ file
    puts "COLLISION: #{file.gsub(/^.*[\/\\]assets[\/\\]/, '')}"
    # 32-bit tiles!!!
    image = ChunkyPNG::Image.from_file file
    tile_rows = image.height / 32
    tile_cols = image.width  / 32
    pixels = image.pixels
    collision_data = []

    # Each tile:
    (0...tile_rows).each do |row|
      (0...tile_cols).each do |col|
        collision = CollisionHeights.new([], [], [], [])
        # Helper function to get the absolute pixel at a given tile-pixel-location
        x_offset = col * 32
        y_offset = row * 32
        pixel_at = lambda do |x, y|
          pixels[(y + y_offset) * image.width + (x + x_offset)]
        end

        # FIRST: Scan DOWN EACH ROW to find downward collision heights
        (0...32).each do |x|
          found = false
          (0...32).each do |y|
            pixel = pixel_at[x, y]
            if (pixel & 0x000000FF) != 0
              collision.top2down << 31 - y
              found = true and break # out of this y-scan to find the next y
            end
          end
          collision.top2down << -1 if !found
        end

        # NEXT: Scan UP EACH ROW to find the upward collision heights
        (0...32).each do |x|
          found = false
          (0...32).reverse_each do |y|
            pixel = pixel_at[x, y]
            if (pixel & 0x000000FF) != 0
              collision.bottom2up << 31 - y
              found = true and break # out of this y-scan to find the next y
            end
          end
          collision.bottom2up << -1 if !found
        end

        # NEXT: Scan RIGHT EACH COLUMN to find the rightward collision heights
        (0...32).reverse_each do |y| # Reverse so that we go down to up (darn y-down convention...)
          found = false
          (0...32).each do |x|
            pixel = pixel_at[x, y]
            if (pixel & 0x000000FF) != 0
              collision.left2right << 31 - x
              found = true and break # out of this x-scan to find the next x
            end
          end
          collision.left2right << -1 if !found
        end

        # NEXT: Scan LEFT EACH COLUMN to find the leftward collision heights
        (0...32).reverse_each do |y| # Reverse so that we go down to up (darn y-down convention...)
          found = false
          (0...32).reverse_each do |x|
            pixel = pixel_at[x, y]
            if (pixel & 0x000000FF) != 0
              collision.right2left << x
              found = true and break # out of this x-scan to find the next x
            end
          end
          collision.right2left << -1 if !found
        end

        collision_data << collision
      end
    end
    all_collision_data[file] = collision_data

    # Don't generate an asset entry for this, since it's not going to be used in raw form in the game
    to_remove << file
    next
  end

  bytes = IO.binread(file)
  assets.write(bytes)

  puts file.gsub! /^.*[\/\\]assets[\/\\]/, ''
  header.write(
    "    // #{file}\n"\
    "    #{current_offset}, #{bytes.size},\n"\
  )

  current_offset += bytes.size
end
all_files -= to_remove

header.write "};\n\n"

all_collision_data.each do |file, heights|
  file = file.gsub(/^.*[\/\\]assets[\/\\]/, '').gsub(/\.collision\.png/, '')
  header.write("const static TileHeights COLLISION_#{ident(file)}[] = {\n")

  heights.each do |collision|
    header.write("    {\n")
    header.write("        { #{collision.top2down.join(', ')} },\n")
    header.write("        { #{collision.bottom2up.join(', ')} },\n")
    # NOTE we wright left2right and right2left in opposite order. It's more intuitive here to say
    # that we _SCAN_ left to right, but this results in a _HEIGHT_ from right to left.
    header.write("        { #{collision.left2right.join(', ')} },\n")
    header.write("        { #{collision.right2left.join(', ')} }\n\n")
    header.write("    },\n")
  end

  header.write("};\n\n")
end

# NOTE that at this point, `all_files` does not actually contain ALL files.. (collision heightmaps are rejected for example)
all_files.each_with_index do |file, index|
  header.write("#define ASSET_#{ident(file)} #{index}\n")
end
header.write("#define NUMBER_OF_ASSETS #{all_files.size}\n\n")

header.write("// Not the most efficient way to get an int from a string\n")
header.write("static int asset_from_ident(const char* ident) {\n")
all_files.each_with_index do |file, index|
  header.write(%<    if (strcmp(ident, "#{ident(file)}") == 0)\n>)
  header.write(%<        return #{index};\n>)
end
header.write("    return -1;\n")
header.write("}\n")

header.write("#endif // ASSETS_H\n")

assets.close
header.close
