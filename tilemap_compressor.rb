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

  puts "==========TILEMAP COMPRESSION INITIAL PASS=========="
  puts repetitions.map { |t| "[#{t.count} #{t.tile_index}]" }.join(' | ')
  puts "=============================="

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

  # Assert that we didn't fuck up
  if (total_count = whole_map.map(&:count).reduce(0, :+)) != tilemap_data.size
    raise "Uhhhh, total map count (#{total_count}) is not equal to the tilemap "\
          "size (#{tilemap_data.size})"
  end

  whole_map
end
