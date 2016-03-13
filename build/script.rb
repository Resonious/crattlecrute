puts "we're in"

def update(game)
  if game.controls.just_pressed(Controls::W)
    puts game.world.local_character.body_type = :young
    puts game.world.local_character.feet_type = :young
  end
  if game.controls.just_pressed(Controls::A)
    puts game.world.local_character.body_type = :standard
    puts game.world.local_character.feet_type = :standard
  end
end
