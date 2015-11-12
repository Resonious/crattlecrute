require_relative 'compile_assets.rb'

def debug_flags
    return if ARGV.include? 'release'
    '-g -O0'
end

if ARGV.include? 'noembed'
    system "gcc src/*.c -ISTB -ISDL/include -lSDL2 -lm -o build/crattlecrute #{debug_flags}"
else
    system "ld -r -b binary -o build/crattlecrute.assets.o build/crattlecrute.assets"
    system "gcc src/*.c build/crattlecrute.assets.o -ISTB -ISDL/include -lSDL2 -lm -DEMBEDDED_ASSETS -o build/crattlecrute #{debug_flags}"
    File.unlink 'build/crattlecrute.assets', 'build/crattlecrute.assets.o'
end
puts "DONE"
