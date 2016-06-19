require_relative 'compile_assets.rb'

def debug_flags
  f = ''
  if ARGV.include? 'release'
    # TODO benchmark/compare O2 and O3 performance
    f += '-O2 -DNDEBUG'
  else
    f += '-g -O0 -D_DEBUG -DDRAW_FPS'
  end

  f += ' -Dabd_assert=SDL_assert'
end

def mruby_path
    if ARGV.include? 'release'
        'MRuby/build/host/lib'
    else
        'MRuby/build/host-debug/lib'
    end
end

if ARGV.include? 'noembed'
    system "gcc src/*.c -std=c99 -ISTB -ISDL/include -lSDL2 -lm -o build/crattlecrute #{debug_flags}"
else
    system "ld -r -b binary -o build/crattlecrute.assets.o build/crattlecrute.assets"
    system "gcc src/*.c -std=c99 build/crattlecrute.assets.o -ISTB -ISDL/include -IMRuby/include -IPCG -L#{mruby_path} -lSDL2 -lm -lmruby -DEMBEDDED_ASSETS -o build/crattlecrute #{debug_flags}"
    File.unlink 'build/crattlecrute.assets', 'build/crattlecrute.assets.o'
end
puts "DONE"
