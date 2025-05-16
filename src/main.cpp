#include "CoFrance.h"

extern "C" PLUGIN_API PluginSDK::BasePlugin *CreatePluginInstance()
{
    try
    {
        return new CoFrancePlugin();
    }
    catch (const std::exception &e)
    {
        return nullptr;
    }
}