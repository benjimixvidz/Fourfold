#pragma once
#include <rack.hpp>

extern rack::Plugin* pluginInstance;
extern rack::Model* modelTetrad;
extern rack::Model* modelTetradPipe;

inline std::shared_ptr<rack::window::Font> panelFont()
{
    return APP->window->loadFont(rack::asset::system("res/fonts/ShareTechMono-Regular.ttf"));
}
