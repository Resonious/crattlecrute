# crattlecrute

A hobby project of mine to make a game with minimal third party support.

It's not completely from scratch though. Uses SDL2, MRuby, stb_image, and the minimal C implementation of the PCG random number generator.

Assets are packaged into one big blob and then embedded into the executable, and all libraries are either statically linked or their source code is just tossed into the reset of the project. This means you end up with just one self-contained executable which I think is pretty cool. Only problem is, I couldn't quite figure out how to get the executable to be TOTALLY dependenciless on Windows... Still gotta link with the MSVC runtime thing.
