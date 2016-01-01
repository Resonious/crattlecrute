TileRepetition = Struct.new(:first_index, :count, :tile_index)
TileAlternation = Struct.new(:first_index, :count)

def compress_tilemap_data(tilemap_data)
  current_tile_index    = nil
  last_tile_index       = tilemap_data[0]
  last_tile_index_count = 1

  # FIRST PASS: count repetitions
  repetitions = []

  i = 1
  while i < tilemap_data.size
    current_tile_index = tilemap_data[i]

    if current_tile_index == last_tile_index
      last_tile_index_count += 1
    else
      repetitions << TileRepetition.new(
        i - last_tile_index_count,
        last_tile_index_count,
        last_tile_index
      )

      last_tile_index_count = 1
    end

    last_tile_index = current_tile_index
    i += 1
  end

  # Add leftovers
  repetitions << TileRepetition.new(
    i - last_tile_index_count,
    last_tile_index_count,
    last_tile_index
  )

=begin
  puts "==========TILEMAP COMPRESSION INITIAL PASS=========="
  puts repetitions.map { |t| "[#{t.count} #{t.tile_index}]" }.join(' | ')
  puts "=============================="
=end

  # SECOND PASS: replace some repetitions with alternations
  whole_map = []

  current_rep = nil
  rep_of_one_count     = 0
  alternation_count    = 0
  building_alternation = false

  i = 0
  while i < repetitions.size
    current_rep = repetitions[i]

    if current_rep.count == 1
      # Always start (or continue) building alternation on reps of 1
      # (A repetition of 1 and an alternation of 1 are the same size)
      rep_of_one_count += 1
      building_alternation ||= true

    elsif building_alternation
      # If we're already "building", and current rep count isn't 1, we
      # have some options.

      if current_rep.count == 2
        # If this rep is a 2, it's just as good to toss it into the
        # current alt as it is to stop the alt (maybe even better)
        rep_of_one_count += 1
      else
        # If it's more than 2, we are no longer saving space by keeping
        # it in the alternation.
        reps_in_alt = repetitions[(i - rep_of_one_count)...i]
        alt = TileAlternation.new(
          reps_in_alt.first.first_index,
          reps_in_alt.map(&:count).reduce(0, :+)
        )

        # Since current_rep is the the reason we want to stop building the alt,
        # `alt` does not include the current_rep.
        whole_map += [alt, current_rep]

        building_alternation = false
        rep_of_one_count = 0
      end
    else# at this point: (current_rep.count != 1) and (building_alternation == false)
      whole_map << current_rep
    end

    i += 1
  end

  # Finish up any trailing alternations
  if building_alternation
    reps_in_alt = repetitions[(i - rep_of_one_count)...i]
    whole_map << TileAlternation.new(
      reps_in_alt.first.first_index,
      reps_in_alt.map(&:count).reduce(0, :+)
    )
  end

=begin
  puts "==========TILEMAP COMPRESSION SECOND PASS=========="
  to_print = whole_map.map do |t|
    case t
    when TileRepetition  then "[#{t.count} #{t.tile_index}]"
    when TileAlternation 
      tile_indices = tilemap_data[t.first_index...(t.first_index + t.count)]
      "[-#{t.count} (#{tile_indices.join(', ')})]"
    end
  end
  puts to_print.join(' | ')
  puts "=============================="
=end

  # Assert that we didn't fuck up
  if (total_count = whole_map.map(&:count).reduce(0, :+)) != tilemap_data.size
    raise "Uhhhh, total map count (#{total_count}) is not equal to the tilemap "\
          "size (#{tilemap_data.size})"
  end

  whole_map
end

# Returns a 'Map' struct with the fields:
# layers, tiles_high, tiles_wide, name, filename
def read_tmx(file)
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

  Struct.new(:layers, :tiles_high, :tiles_wide, :name, :filename)
        .new( layers,  tiles_high,  tiles_wide, map_name, filename)
end
