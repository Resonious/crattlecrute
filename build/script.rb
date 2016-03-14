puts "we're in"

def update(game)
  begin
    if game.controls.just_pressed(Controls::W)
      game.world.local_character.body_type = :young
      game.world.local_character.feet_type = :young
    end
    if game.controls.just_pressed(Controls::A)
      game.world.local_character.body_type = :standard
      game.world.local_character.feet_type = :standard
    end
    if game.controls.just_pressed(Controls::D)
      game.world.local_character.body_color.r = 255
      puts game.world.local_character.body_color.inspect
    end
  rescue Exception => e
    puts "LMAO im dumb"
  end
end
