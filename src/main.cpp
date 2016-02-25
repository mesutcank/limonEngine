//
// Created by Engin Manap on 10.02.2016.
//

#include <iostream>

#include "GLHelper.h"
#include "SDL2Helper.h"
#include "World.h"
#include "InputHandler.h"

#define PROGRAM_NAME "UberGame"

int main(int argc, char *argv[]){
    int height=768, width=1024;

    bool quit=false;

    SDL2Helper sdlHelper(PROGRAM_NAME, height,width);

    GLHelper glHelper;
    glHelper.reshape(height,width);
    InputHandler inputHandler(sdlHelper.getWindow(), height,width);

    World world(&glHelper);

    Uint32 ticks;
    while(!inputHandler.getInputStatus(inputHandler.QUIT)){
        glHelper.clearFrame();
        ticks = SDL_GetTicks();
        world.play(ticks, inputHandler);
        world.render();
        sdlHelper.swap();
        inputHandler.mapInput();
    }
    return 0;
}

