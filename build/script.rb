puts "we're in"

def update(game)
  puts "ok" if game.controls.just_pressed(Controls::A)
  if game.controls.just_pressed(Controls::W)
    puts "========="
    puts game.world.current_map.game.inspect
    puts game.inspect
  end
end
