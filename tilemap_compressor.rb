TileRepetition = Struct.new(:first_index, :count, :tile_index)
TileAlternation = Struct.new(:first_index, :count)

Tileset = Struct.new(:firstgid, :name, :tilewidth, :tileheight, :tilecount, :filename, :tiles_per_row)
Layer = Struct.new(:name, :width, :height, :raw_data, :sublayers) do
  def collision?; name == 'collision'; end
end
Sublayer = Struct.new(:tileset, :data, :compressed_data)
ImageLayer = Struct.new(
  :name, :filename, :x, :y, :parallax_factor,
  :frame_height, :frame_width, :frames, :wrap_x, :wrap_y
)
MapObject = Struct.new(:id, :name, :type, :x, :y, :width, :height, :properties)
SpawnRate = Struct.new(:mob_name, :percentage)

IMAGE_LAYER_DEFAULTS = {
  parallax_factor: 1,
  frame_width: 0,
  frame_height: 0,
  frames: 1,
  wrap_x: 'false',
  wrap_y: 'false'
}

def config_bool(value)
  value.downcase == 'true' || value.to_i == 1
end

def ident(file)
  file
    .gsub(/\.\.\//, '')
    .gsub(/[\.\s\?!\/\\-]/, '_')
    .upcase
end

# Straight up parse game.h to get IDs for area strings ... I just don't feel like
# having a string->id table for runtime.
def parse_enum(enum, file_content)
  result = Hash.new(-1)
  if match = /enum #{enum} {(?<entries>[^{}]+)}/m.match(file_content)
    i = 0

    match[:entries].split(",").each do |entry|
      entry = entry.strip
      next if entry.empty?
      if /=\s+(?<set_to>-?\d+)/ =~ entry
        i = set_to.to_i
        entry = entry.gsub(/\s*=.*$/, '')
      end

      result[entry] = i
      i += 1
    end
  else
    raise "Fucked up reading game.h (or whatever this file is)!"
  end

  result
end

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
  elsif map.attributes['tilewidth'].value != '32' || map.attributes['tileheight'].value != '32'
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
      image.attributes['source'].value.gsub('assets/', ''),
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
        puts "WARNING: #{filename} layer \"#{layer.name}\" tile #{i} is y-flipped - ignoring this flip."
        flags &= ~(1 << 6)
      end

      tileset = tilesets.find do |t|
        global_index - t.firstgid >= 0
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

  # ===== Grab imagelayers for parallax backgrounds =====
  image_layers = []
  map.css('imagelayer').each do |imagelayer|
    attrs = imagelayer.attributes

    struct = ImageLayer.new
    struct.name = attrs['name'].value
    struct.x = attrs['x'] ? attrs['x'].value : '0'
    struct.y = attrs['y'] ? attrs['y'].value : '0'
    struct.filename = imagelayer.css('image').first.attributes['source'].value

    IMAGE_LAYER_DEFAULTS.each do |field, default_value|
      if element = imagelayer.css("properties property[name='#{field}']").first
        value = element.attributes['value'].value
      else
        value = default_value
      end
      struct.send("#{field}=", value)
    end

    image_layers << struct
  end

  # ===== Grab object layers for whatever =====
  map_height = tiles_high.to_i * 32
  map_objects = []
  map.css('objectgroup object').each do |object|
    attrs = object.attributes

    struct = MapObject.new
    struct.id = attrs['id'].value
    struct.name = attrs['name'] ? attrs['name'].value : '<unnamed>'
    struct.x = (attrs['x'] ? attrs['x'].value : '0').to_i
    struct.y = map_height - (attrs['y'] ? attrs['y'].value : '0').to_i
    struct.width  = (attrs['width']  ?  attrs['width'].value : '0').to_i
    struct.height = (attrs['height'] ? attrs['height'].value : '0').to_i
    struct.type = attrs['type'].value.downcase.to_sym if attrs['type']
    struct.properties = {}

    object.css('properties property').each do |property|
      prop_attrs = property.attributes
      struct.properties[prop_attrs['name'].value] = prop_attrs['value'].value
    end

    map_objects << struct
  end

  # === DONE ===
  Struct.new(:layers, :image_layers, :map_objects, :tiles_high, :tiles_wide, :name, :filename, :height)
        .new( layers, image_layers,  map_objects,  tiles_high,  tiles_wide, map_name, filename, map_height)
end

def write_cm(map, file_dest)
  collision_layer      = map.layers.find(&:collision?)
  non_collision_layers = map.layers.reject(&:collision?)

  doors     = map.map_objects.select { |o| o.type == :door }
  mob_zones = map.map_objects.select { |o| o.type == :spawn }

  # We need to collect all spawn rates ahead of time so that we know
  # how much memory to allocate ahead of time when reading the file.
  all_spawn_rates = {}
  mob_zones.each do |mob_spawnzone|
    spawn_rates = []

    mob_spawnzone.properties.each do |name, value|
      next unless value.include?('%')
      percentage = value.gsub('%', '').to_i
      next if percentage == 0

      spawn_rates << SpawnRate.new(name, percentage)
    end

    all_spawn_rates[mob_spawnzone.id] = spawn_rates
  end

  # ============================================================================
  # ================================== FILE WRITE START ========================
  # ============================================================================
  file = File.new(file_dest, 'wb')

  number_of_sublayers = 0
  non_collision_layers.each do |layer|
    number_of_sublayers += layer.sublayers.size
  end

  raise "no collision layer!!" if collision_layer.nil?
  collision_sublayer = collision_layer.sublayers.values.first

  # ==== HEADER ====
  file.write('CM1') # Magic (and version number I guess lol)
  file.write(
    # Tilemap width and height (both Uint32)
    [map.tiles_wide, map.tiles_high].pack('LL')
  )
  # Number of sublayers (tilemaps): Uint8
  file.write([number_of_sublayers].pack('C'))
  # Number of imagelayers (parallax backgrounds): Uint8
  file.write([map.image_layers.size].pack('C'))
  # Number of doors: Uint8
  file.write([doors.size].pack('C'))
  # Total Number of (mob, percentage) spawn rate pairs: Uint16
  file.write([all_spawn_rates.map(&:last).map(&:size).reduce(0, :+)].pack('S'))
  # Number of mob spawn zones: Uint8
  file.write([mob_zones.size].pack('C'))

  non_collision_layers.each do |layer|
    layer.sublayers.values.each do |sublayer|
      # === Sublayer header ===
      sublayer_header_assetname = ident(sublayer.tileset.filename).bytes
      sublayer_header_assetname << 0 # terminating zero
      file.write(
        # c string with terminating zero (from above)
        sublayer_header_assetname.pack(
          'C' * sublayer_header_assetname.size
        )
      )

      sublayer_header_data = [
        sublayer.tileset.tiles_per_row,
        sublayer.compressed_data.size
      ]
      file.write(
        # tiles per row (Uint16),
        # number of 32bit signed integers in tilemap data (Uint32)
        sublayer_header_data.pack(
          'SL'
        )
      )

      # === Sublayer data ===
      file.write(
        # compressed tilemap data (int32[])
        sublayer.compressed_data.pack(
          'l' * sublayer.compressed_data.size
        )
      )
    end
  end

  # === Collision data ===
  # number of 32bit signed integers in collision data (Uint32)
  file.write(
    [collision_sublayer.data.size].pack('L')
  )
  # uncompressed collision data
  file.write(
    collision_sublayer.data.pack(
      'l' * collision_sublayer.data.size
    )
  )

  # === Parallax Backgrounds ===
  map.image_layers.each do |image_layer|
    bg_header_assetname = ident(image_layer.filename).gsub(/^ASSETS_/, '').bytes
    bg_header_assetname << 0 # terminating zero
    file.write(
      # c string with terminating zero (from above)
      bg_header_assetname.pack(
        'C' * bg_header_assetname.size
      )
    )

    bit_fields = 0
    bit_fields |= 1 << 0 if config_bool(image_layer.wrap_x)
    bit_fields |= 1 << 1 if config_bool(image_layer.wrap_y)

    file.write(
      [
        image_layer.x.to_i,               # int32
        image_layer.y.to_i,               # int32
        image_layer.parallax_factor.to_f, # float32
        image_layer.frame_width.to_i,     # int32
        image_layer.frame_height.to_i,    # int32
        image_layer.frames.to_i,          # uint32
        bit_fields                        # uint32
      ]
        .pack("llfllLL")
    )
  end

  # === DOORS!!! ===
  game_h = File.open(File.join(File.dirname(__FILE__), 'src/game.h'), &:read)
  area_ids = parse_enum('AreaId', game_h)

  doors.each do |door|
    ['leads_to_area', 'leads_to_x', 'leads_to_y'].each do |x|
      if door.properties[x].nil?
        raise "bad door \"#{door.name}\" is missing property: #{x}"
      end
    end

    leads_to_area = area_ids[door.properties['leads_to_area']]
    leads_to_x    = eval(door.properties['leads_to_x'])
    leads_to_y    = eval(door.properties['leads_to_y'])

    file.write(
      [
        door.x.to_i,        # int32
        door.y.to_i,        # int32
        leads_to_area.to_i, # int32
        leads_to_x.to_i,    # int32
        leads_to_y.to_i     # int32
      ]
        .pack("lllll")
    )
  end

  # === MOBS!!! ===
  mob_h = File.open(File.join(File.dirname(__FILE__), 'src/mob.h'), &:read)
  mob_ids = parse_enum('MobId', mob_h)

  mob_zones.each do |mob_spawnzone|
    # x,y,w,h all int32
    file.write(
      [
        mob_spawnzone.x,                        # int32
        mob_spawnzone.y - mob_spawnzone.height, # int32
        mob_spawnzone.width,                    # int32
        mob_spawnzone.height                    # int32
      ]
        .pack("llll")
    )

    spawn_rates = all_spawn_rates[mob_spawnzone.id]

    # Number of mobs that can spawn (Uint8)
    file.write([spawn_rates.size].pack('C'))

    # Spawnable mobs each (int32, int32)
    spawn_rates.each do |spawn|
      file.write(
        [
          mob_ids[spawn.mob_name].to_i, # int32
          spawn.percentage.to_i         # int32
        ]
          .pack("ll")
      )
    end
  end

ensure
  file.close rescue false
end
