#!/usr/bin/env sh

# Compile the platformer using g++ with SDL2 and OpenCV

# Adjust pkg-config to include both opencv4 and sdl2

g++ main.cpp -o simulacion -std=c++17 `pkg-config --cflags --libs opencv4 sdl2`

