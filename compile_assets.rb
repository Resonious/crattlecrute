require 'fileutils'
require 'chunky_png'
require 'nokogiri'

assets_base_dir = File.dirname File.expand_path __FILE__

FileUtils.mkdir_p "#{assets_base_dir}/build"

# Open files for the .h and the assets themselves.
assets = File.new("#{assets_base_dir}/build/crattlecrute.assets", 'wb')
header = File.new("#{assets_base_dir}/src/assets.h", 'w')

header.write("#ifndef ASSETS_H\n")
header.write("#define ASSETS_H\n\n")
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
CollisionHeights = Struct.new(:top2down, :bottom2up, :left2right, :right2left)
all_collision_data = {}
tilemap_data = [] # TODO this should be handled differently..
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
              collision.top2down << 32 - y
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
              collision.bottom2up << 32 - y
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
              collision.left2right << 32 - x
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

    # Don't generate an asset entry for this, since it's not going to be in the assets file
    to_remove << file
    next

  # Handle tmx files separately
  elsif /^.+\.tmx$/ =~ file
    filename = File.basename(file)

    tmx = File.open(file) { |f| Nokogiri::XML(f) }
    map_count = tmx.css('map').count
    map = tmx.css('map').first

    # Make sure that sucker is valid
    raise "More than one map in a tmx?? #{filename}" if map_count > 1
    raise "No maps in this tmx?? #{filename}"        if map_count < 1

    if map.attributes['orientation'].value != 'orthogonal'
      raise "The tilemap #{filename} isn't orthogonal"
    elsif map.attributes['tilewidth'].value =! '32' || map.attributes['tileheight'].value != '32'
      raise "The tilemap #{filename} doesn't use 32x32 tiles"
    end

    # We will have to generate multiple tile map arrays for each tileset...
    # Let's just assume one tileset for now (TODO)
    raise "Gotta implement multiple tilesets dude" if map.css('tileset').size > 1

    # Gotta subtract this from all data entries to get the right index
    first_gid = map.css('tileset').first.attributes['firstgid'].value.to_i

    # Let's also just not give a fuck about tileset for now actually

    # Let's ALSO assume one layer
    raise "Gotta implement multiple layers dude" if map.css('layer').size > 1

    data = map.css('layer > data').first
    raise "Gotta be CSV :(" if data.attributes['encoding'].value != 'csv'

    out_data = []

    data.content.split(',').each do |num|
      num = num.to_i
      out_data << -1 and next if num == 0

      index = (num & 0x00FFFFFF) - first_gid
      flags = (num & 0xFF000000) >> 24

      raise "DON'T Y-FLIP!!!!" if (flags & (1 << 6)) != 0

      out_data << (index | (flags << 24))
    end

    tiles_wide = map.attributes['width'].value.to_i
    tiles_high = map.attributes['height'].value.to_i
    # TODO handle this differently
    (0...tiles_wide).each do |x|
      (0...tiles_high).each do |y|
        tilemap_data[y * tiles_wide + x] = out_data[(tiles_high - y - 1) * tiles_wide + x]
      end
    end

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

# TODO here's the final part to the tilemap data situation
header.write("const static int TEST_TILEMAP[] = {\n    ")
tilemap_data.each_with_index do |tile_index, i|
  header.write("#{tile_index},")
  header.write((i + 1) % 20 == 0 ? "\n    " : " ") unless i == tilemap_data.size - 1
end
header.write("\n};\n\n")

header.write(
  "typedef struct {\n"\
  "    int top2down[32];\n"\
  "    int bottom2up[32];\n"\
  "    int right2left[32];\n"\
  "    int left2right[32];\n"\
  "} TileHeights;\n\n"
)
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

# NOTE that at this point, `all_files` does not actually contain ALL files..
all_files.each_with_index do |file, index|
  header.write("const static int ASSET_#{ident(file)} = #{index};\n")
end
header.write("const static int NUMBER_OF_ASSETS = #{all_files.size};\n\n")
header.write("#endif // ASSETS_H\n")

assets.close
header.close
