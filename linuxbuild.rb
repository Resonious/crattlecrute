require_relative 'compile_assets.rb'
system "gcc src/*.c -ISTB -ISDL/include -lSDL2 -lm -o build/crattlecrute"
puts "DONE"
