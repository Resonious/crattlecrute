puts "we're in"

def guy
  world.local_character
end

def inv
  world.local_character.inventory
end

CTRL_SYMS = {
  jump:  Controls::JUMP,
  run:   Controls::RUN,
  up:    Controls::UP,
  down:  Controls::DOWN,
  left:  Controls::LEFT,
  right: Controls::RIGHT,
  w:     Controls::W,
  a:     Controls::A,
  s:     Controls::S,
  d:     Controls::D
}

# Quick helpers for #press and #after down there
module TimeUnits
  def frames
    self
  end
  def frame
    self
  end

  def seconds
    self * 60
  end
  def second
    seconds
  end

  def minutes
    self * 60 * 60
  end
  def minute
    minutes
  end

  def in_seconds
    self / 60
  end
  def in_minutes
    self / 60 / 60
  end
end

Fixnum.send(:include, TimeUnits)
Float.send(:include, TimeUnits)

class ControlPress
  attr_reader :controls
  attr_reader :control
  attr_reader :control_sym
  attr_accessor :countdown

  def initialize(controls, control_sym, countdown_frames)
    @controls    = controls
    @control_sym = control_sym
    @control     = CTRL_SYMS[control_sym]
    @countdown   = countdown_frames
  end

  def inspect
    "#<ControlPress #{control_sym}:#{countdown}>"
  end
end

class DelayedAction
  attr_accessor :countdown
  attr_reader :block
  attr_reader :args

  def initialize(countdown, block, args)
    @countdown = countdown
    @block     = block
    @args      = args
  end

  def inspect
    "#<DelayedAction #{args.size}:#{countdown}>"
  end
end

class OnUpdate
  attr_accessor :block
  attr_accessor :args

  def initialize(b, *args)
    @block = b
    @args = args
  end

  def inspect
    "#<OnUpdate (#{args.size})>"
  end
end

class Done < StandardError
end

@control_presses = []
@delayed_actions = []
@on_updates = []

def press(control_sym, options = {})
  @controls ||= game.controls
  frames = options[:for] || 1
  @control_presses << ControlPress.new(@controls, control_sym, frames)
end

def after(frames, *args, &action)
  @delayed_actions << DelayedAction.new(frames, action, args)
end

def every(frame_interval, *args, &action)
  block = proc do |*a|
    action.call(*a)
    after(frame_interval, *a, &block)
  end
  block.call(*args)
end

def on_update(*args, &block)
  @on_updates << OnUpdate.new(block, *args)
end

# Some extensions..
class Crattlecrute
  def change
    yield self
    mark_dirty
  end
end

def phash(h)
  puts "-----------------------------"
  h.each do |name, seconds|
    if block_given?
      next unless yield(name.to_s)
    end
    puts "#{name} ==> #{seconds} seconds"
    puts "-----------------------------"
  end
  nil
end

def average_bench_over(time)
  bench  = game.bench
  totals = Hash.new(0.0)
  count  = 0

  on_update do
    bench.each do |name, seconds|
      totals[name] += seconds
    end
    count += 1

    if count >= time
      puts "-----------------------------"
      totals.each do |name, seconds|
        puts "#{name} ==> #{seconds / count} seconds"
        puts "-----------------------------"
      end

      raise Done
    end
  end
end
alias avg_bench average_bench_over

def peak_bench_over(time, &block)
  bench = game.bench
  peaks = Hash.new(0)
  count = 0

  on_update do
    bench.each do |name, seconds|
      existing = peaks[name]
      peaks[name] = seconds if seconds > existing
    end
    count += 1

    if count >= time
      phash(peaks, &block)

      raise Done
    end
  end
end

def random_genes
  (Random.rand * 2000000000).to_i
end

def test_egg
  e = Egg.new
  e.age = 0
  e.hatching_age = 2.seconds
  e.genes = random_genes | GSPEC_SOLID_COLOR | GSPEC_DARKER_COLOR
  e
end

def goto_test
  world.move_to 0, [200, 200]
end

def inc_hue(plr, amount)
  h = plr.body_color.hue
  h += amount
  hue_color = h.as_hue
  plr.body_color.r = hue_color.r
  plr.body_color.g = hue_color.g
  plr.body_color.b = hue_color.b
end

def monitor_local
  every 1.second do
    puts world.local_character.position.inspect
  end
end

def monitor_remote(index = 0)
  every 1.second do
    puts world.remote_characters[index].position.inspect if world.remote_characters[index]
  end
end

def update(game)
  @control_presses.delete_if do |c|
    begin
      if c.countdown <= 0
        c.controls[c.control] = false
        true
      else
        c.controls[c.control] = true
        c.countdown -= 1
        false
      end
    rescue StandardError => e
      puts "CONTROL PRESS: #{e.class}: #{e.message}"
      true
    end
  end
  @delayed_actions.delete_if do |a|
    begin
      if a.countdown <= 0
        a.block.call(*a.args)
        true
      else
        a.countdown -= 1
        false
      end
    rescue StandardError => e
      puts "DELAYED ACTION: #{e.class}: #{e.message}"
      true
    end
  end
  @on_updates.delete_if do |u|
    begin
      u.block.call(*u.args)
      false
    rescue Done => _
      true
    rescue StandardError => e
      puts "ON-UPDATE: #{e.class}: #{e.message}"
      true
    end
  end

=begin
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
=end
end
