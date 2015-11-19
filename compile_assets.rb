require 'fileutils'
require 'chunky_png'

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
CollisionHeights = Struct.new(:top2down, :bottom2up, :left2right, :right2left)
all_collision_data = {}
to_remove = []
all_files.each do |file|
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
        pixel_at = lambda do |x, y|
          x_offset = col * 32
          y_offset = row * 32
          pixels[(y + y_offset) * image.width + (x + x_offset)]
        end

        # FIRST: Scan DOWN EACH ROW to find downward collision heights
        (0...32).each do |x|
          found = false
          (0...32).each do |y|
            pixel = pixel_at[x, y]
            if pixel != 0xFFFFFF00
              collision.top2down << image.height - y
              found = true and break # out of this y-scan to find the next y
            end
          end
          collision.top2down << 0 if !found
        end

        # NEXT: Scan UP EACH ROW to find the upward collision heights
        (0...32).each do |x|
          found = false
          (0...32).reverse_each do |y|
            pixel = pixel_at[x, y]
            if pixel != 0xFFFFFF00
              collision.bottom2up << image.height - y
              found = true and break # out of this y-scan to find the next y
            end
          end
          collision.bottom2up << 0 if !found
        end

        # NEXT: Scan RIGHT EACH COLUMN to find the rightward collision heights
        (0...32).reverse_each do |y| # Reverse so that we go down to up (darn y-down convention...)
          found = false
          (0...32).each do |x|
            pixel = pixel_at[x, y]
            if pixel != 0xFFFFFF00
              collision.left2right << x
              found = true and break # out of this x-scan to find the next x
            end
          end
          collision.left2right << 0 if !found
        end

        # NEXT: Scan LEFT EACH COLUMN to find the leftward collision heights
        (0...32).reverse_each do |y| # Reverse so that we go down to up (darn y-down convention...)
          found = false
          (0...32).reverse_each do |x|
            pixel = pixel_at[x, y]
            if pixel != 0xFFFFFF00
              collision.right2left << x
              found = true and break # out of this x-scan to find the next x
            end
          end
          collision.right2left << 0 if !found
        end

        collision_data << collision
      end
    end
    all_collision_data[file] = collision_data

    # Don't generate an asset entry for this, since it's not in the assets file
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

header.write(
  "typedef struct {\n"\
  "    int top2down[32];\n"\
  "    int bottom2up[32];\n"\
  "    int left2right[32];\n"\
  "    int right2left[32];\n"\
  "} TileHeights;\n\n"
)
all_collision_data.each do |file, heights|
  file = file.gsub(/^.*[\/\\]assets[\/\\]/, '').gsub(/\.collision\.png/, '')
  header.write("const static TileHeights COLLISION_#{ident(file)}[#{heights.size}] = {\n")

  heights.each do |collision|
    header.write("    {\n")
    header.write("        { #{collision.top2down.join(', ')} },\n")
    header.write("        { #{collision.bottom2up.join(', ')} },\n")
    header.write("        { #{collision.left2right.join(', ')} },\n")
    header.write("        { #{collision.right2left.join(', ')} }\n\n")
    header.write("    },\n")
  end

  header.write("};\n\n")
end

all_files.each_with_index do |file, index|
  header.write("const static int ASSET_#{ident(file)} = #{index};\n")
end

assets.close
header.close
