require_relative 'compile_assets.rb'
if ARGV.first && ARGV.first == 'embed'
    system "ld -r -b binary -o build/crattlecrute.assets.o build/crattlecrute.assets"
    system "gcc src/*.c build/crattlecrute.assets.o -ISTB -ISDL/include -lSDL2 -lm -DEMBEDDED_ASSETS -o build/crattlecrute"
    File.unlink 'build/crattlecrute.assets', 'build/crattlecrute.assets.o'
else
    system "gcc src/*.c -ISTB -ISDL/include -lSDL2 -lm -o build/crattlecrute"
end
puts "DONE"
