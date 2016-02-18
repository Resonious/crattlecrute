require_relative 'compile_assets.rb'

def debug_flags
    if ARGV.include? 'release'
      # TODO benchmark/compare O2 and O3 performance
      '-O2 -DNDEBUG'
    else
      '-g -O0 -D_DEBUG -DDRAW_FPS'
    end
end

if ARGV.include? 'noembed'
    system "gcc src/*.c -ISTB -ISDL/include -lSDL2 -lm -o build/crattlecrute #{debug_flags}"
else
    system "gcc src/*.c -LSDL -LSDL/build -ISTB -ISDL/include -lSDL2 -lm -DEMBEDDED_ASSETS -o build/crattlecrute #{debug_flags} -sectcreate assets assets build/crattlecrute.assets"
end
puts "DONE"
