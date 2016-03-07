puts "we're in"

def update(game)
  puts "ok" if game.controls.just_pressed(Controls::A)
  puts game.world.inspect if game.controls.just_pressed(Controls::W)
end
