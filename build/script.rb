puts "we're in"

def guy
  world.local_character
end

CTRL_SYMS = {
  jump:  Controls::JUMP,
  run:   Controls::RUN,
  up:    Controls::UP,
  down:  Controls::DOWN,
  left:  Controls::LEFT,
  right: Controls::RIGHT
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

@control_presses = []
@delayed_actions = []

def press(control_sym, options = {})
  frames = options[:for] || 1
  @control_presses << ControlPress.new(game.controls, control_sym, frames)
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

class Crattlecrute
  def change
    yield self
    mark_dirty
  end
end

def update(game)
  @control_presses.delete_if do |c|
    if c.countdown <= 0
      c.controls[c.control] = false
      true
    else
      c.controls[c.control] = true
      c.countdown -= 1
      false
    end
  end
  @delayed_actions.delete_if do |a|
    if a.countdown <= 0
      a.block.call(*a.args)
      true
    else
      a.countdown -= 1
      false
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
