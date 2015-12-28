require 'fileutils'
require 'chunky_png'
require 'nokogiri'
require_relative 'tilemap_compressor'

Tileset = Struct.new(:firstgid, :name, :tilewidth, :tileheight, :tilecount, :filename, :tiles_per_row)
Layer = Struct.new(:name, :width, :height, :raw_data, :sublayers) do
  def collision?; name == 'collision'; end
end
Sublayer = Struct.new(:tileset, :data, :compressed_data)

assets_base_dir = File.dirname File.expand_path __FILE__

FileUtils.mkdir_p "#{assets_base_dir}/build"

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

def ident(file)
  file.gsub(/\.{2}\//, '').gsub(/[\.\s\?!\/\\-]/, '_').upcase
end

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

    # Don't generate an asset entry for this, since it's not going to be in the assets file
    to_remove << file
    next

  # Handle tmx files separately
  elsif /^.+\.tmx$/ =~ file
    begin
      filename = File.basename(file)
      map_name = filename.gsub('.tmx', '')

      tmx = File.open(file) { |f| Nokogiri::XML(f) }
      map_count = tmx.css('map').count
      map = tmx.css('map').first

      tiles_wide = map.attributes['width'].value.to_i
      tiles_high = map.attributes['height'].value.to_i

      # ========== Make sure that sucker is valid ==============
      raise "No maps in this tmx?? #{filename}"        if map_count < 1
      raise "More than one map in a tmx?? #{filename}" if map_count > 1

      if map.attributes['orientation'].value != 'orthogonal'
        raise "The tilemap #{filename} isn't orthogonal"
      elsif map.attributes['tilewidth'].value =! '32' || map.attributes['tileheight'].value != '32'
        raise "The tilemap #{filename} doesn't use 32x32 tiles"
      end

      # ============ Grab tilesets =============
      tilesets = []
      map.css('tileset').each do |tileset|
        attrs = tileset.attributes
        image = tileset.css('image').first # assuming one image lol...

        tilesets << Tileset.new(
          attrs['firstgid'].value.to_i,
          attrs['name'].value,
          attrs['tilewidth'].value.to_i,
          attrs['tileheight'].value.to_i,
          attrs['tilecount'].value.to_i,
          image.attributes['source'].value,
          image.attributes['width'].value.to_i / 32
        )
      end
      # First tileset should be the one with the largest firstgid
      tilesets.sort! { |a, b| b.firstgid <=> a.firstgid }

      # ============== Grab layers ================
      layers = []
      found_collision_layer = false
      map.css('layer').each do |layer|
        attrs = layer.attributes
        data = layer.css('data').first # Assuming 1 data lol
        raise "Tile encoding must be csv #{filename}" if data.attributes['encoding'].value != 'csv'

        layers << Layer.new(
          attrs['name'].value.downcase.strip,
          attrs['width'].value.to_i,
          attrs['height'].value.to_i,
          data.content.split(',').map(&:to_i),
          {} # Sublayers come from multiple tilesets being used in one layer
        )

        if layers.last.collision?
          if found_collision_layer
            raise "Two collision layers found in tilemap #{filename}"
          else
            found_collision_layer = true
          end
        end
      end
      raise "No collision layer found in tilemap #{filename} (intentional or no?)" unless found_collision_layer

      # ============ Populate sublayers ============

      layers.each do |layer|
        layer.raw_data.each_with_index do |num, i|
          next if num == 0

          global_index = (num & 0x00FFFFFF)
          flags = (num & 0xFF000000) >> 24

          if (flags & (1 << 6)) != 0
            puts "WARNING: #{filename} layer #{layer.name} tile #{i} is y-flipped - ignoring this flip."
            flags &= ~(1 << 6)
          end

          tileset = tilesets.find do |tileset|
            global_index - tileset.firstgid >= 0
          end
          index = global_index - tileset.firstgid

          layer.sublayers[tileset] ||= Sublayer.new(tileset, Array.new(layer.raw_data.size, -1), [])
          layer.sublayers[tileset].data[i] = (index | (flags << 24))
        end

        # Flip the sublayer data upside-down (for 0=bottom)
        layer.sublayers.values.each do |sublayer|
          new_data = sublayer.data.clone
          (0...tiles_wide).each do |x|
            (0...tiles_high).each do |y|
              new_data[y * tiles_wide + x] = sublayer.data[(tiles_high - y - 1) * tiles_wide + x]
            end
          end
          sublayer.data = new_data
        end

        # Compress sublayer data if necessary
        if layer.collision?
          # Collision layer should not be compressed and only have one sublayer
          raise "collision layer has more than one tileset. #{filename}" if layer.sublayers.size > 1
        else
          # Non-collision layers should all be compressed
          layer.sublayers.each do |tileset, sublayer|
            new_data = []
            compress_tilemap_data(sublayer.data).each do |entry|
              case entry
              when TileRepetition
                new_data += [entry.count, entry.tile_index]

              when TileAlternation
                new_data << -entry.count
                tile_indices = sublayer.data[entry.first_index...(entry.first_index + entry.count)]
                new_data += tile_indices

              else
                raise "what"
              end
            end
            sublayer.compressed_data = new_data
          end
        end
      end

      # ============= Done! The rest of the work is writing to the header file =============
      maps << Struct.new(:layers, :tiles_high, :tiles_wide, :name, :filename)
                    .new( layers,  tiles_high,  tiles_wide, map_name, filename)

    rescue StandardError => e
      puts "-----------------------------------"
      puts "SKIPPING #{file} -- #{e.message}"
      puts e.backtrace.first
      puts "-----------------------------------"
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
  header.write("#define ASSET_#{ident(file)} #{index}\n")
end
header.write("#define NUMBER_OF_ASSETS #{all_files.size}\n\n")

# Tilemap time!!!
def write_int_array(header, arr)
  arr.each_with_index do |num, i|
    header.write("#{num},")
    if (i+1) % 20 == 0
      header.write("\n    ")
    elsif i < arr.size - 1
      header.write(" ")
    end
  end
  header.write("\n")
end

header.write("// NOTE DUDE okay we totally assume that these globals are stored sequentially...\n")
maps.each do |map|
  # map.name, map.filename, map.layers, map.tiles_high, map.tiles_wide

  map_ident = ident(map.name)
  first_tilemap_ident = nil

  collision_layer = map.layers.find(&:collision?)
  non_collision_layers = map.layers.reject(&:collision?)

  # Write collision map first
  header.write("const static int MAP_#{map_ident}_COLLISION[] = {\n    ")
  write_int_array header, collision_layer.sublayers.values.first.data
  header.write("};\n")

  # Write non-collision arrays
  non_collision_layers.each do |layer|
    layer_ident = ident(layer.name)

    layer.sublayers.values.each do |sublayer|
      sublayer_ident = ident(sublayer.tileset.name)

      data_ident = "MAP_#{map_ident}_#{layer_ident}_#{sublayer_ident}_DATA"
      header.write("const static int #{data_ident}[] = {\n    ")
      write_int_array header, sublayer.compressed_data
      header.write("};\n")
    end
  end
  header.write("\n")
  # Write non-collision TileMaps
  non_collision_layers.each do |layer|
    layer_ident = ident(layer.name)

    layer.sublayers.values.each do |sublayer|
      sublayer_ident = ident(sublayer.tileset.name)

      data_ident    = "MAP_#{map_ident}_#{layer_ident}_#{sublayer_ident}_DATA"
      tilemap_ident = "MAP_#{map_ident}_#{layer_ident}_#{sublayer_ident}_TILEMAP"
      first_tilemap_ident ||= tilemap_ident

      header.write("static Tilemap #{tilemap_ident} = {\n")
      # tex_asset, texture
      header.write("    ASSET_#{ident(sublayer.tileset.filename)}, NULL,\n")
      # tiles_per_row, width, height
      header.write("    #{sublayer.tileset.tiles_per_row}, #{map.tiles_wide}, #{map.tiles_high},\n")
      # tiles
      header.write("    #{data_ident}\n")
      header.write("};\n")
    end
  end

  number_of_tilemaps = 0
  non_collision_layers.each do |layer|
    number_of_tilemaps += layer.sublayers.size
  end

  header.write("\n")
  header.write("const static Map MAP_#{map_ident} = {\n")
  # CollisionMap
  collision_file = collision_layer.sublayers.keys.first.filename
    .gsub(/^.*[\/\\]assets[\/\\]/, '').gsub(/\.collision\.png/, '')

  header.write("    {\n")
  header.write("        COLLISION_#{ident(collision_file)},\n")
  header.write("        #{map.tiles_wide}, #{map.tiles_high}, MAP_#{map_ident}_COLLISION\n")
  header.write("    },\n")
  # number_of_tilemaps
  header.write("    #{number_of_tilemaps},\n")
  # Tilemaps
  header.write("    &#{first_tilemap_ident}\n")
  header.write("};\n\n")
end

header.write("#endif // ASSETS_H\n")

assets.close
header.close
