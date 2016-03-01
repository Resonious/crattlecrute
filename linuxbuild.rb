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
    system "gcc src/*.c -std=c99 -ISTB -ISDL/include -lSDL2 -lm -o build/crattlecrute #{debug_flags}"
else
    system "ld -r -b binary -o build/crattlecrute.assets.o build/crattlecrute.assets"
    system "gcc src/*.c -std=c99 build/crattlecrute.assets.o -ISTB -ISDL/include -lSDL2 -lm -DEMBEDDED_ASSETS -o build/crattlecrute #{debug_flags}"
    File.unlink 'build/crattlecrute.assets', 'build/crattlecrute.assets.o'
end
puts "DONE"
