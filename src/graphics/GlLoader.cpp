#include "GlLoader.hpp"

#include <SDL3/SDL_video.h>

#include "Supervisor.hpp"

#define TH_GL_DEFINE(type, name) type th_##name = nullptr;
TH_GL_FUNCTIONS(TH_GL_DEFINE)
#undef TH_GL_DEFINE

bool LoadGlFunctions()
{
    bool ok = true;

#define TH_GL_LOAD(type, name)                                                                                         \
    th_##name = (type)SDL_GL_GetProcAddress(#name);                                                                    \
    if (th_##name == nullptr)                                                                                          \
    {                                                                                                                  \
        Supervisor::DebugPrint("missing GL function %s\n", #name);                                                     \
        ok = false;                                                                                                    \
    }
    TH_GL_FUNCTIONS(TH_GL_LOAD)
#undef TH_GL_LOAD

    return ok;
}
