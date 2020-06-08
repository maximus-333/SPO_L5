// dllmain.cpp : Defines the entry point for the DLL application.

//This is needed for unchanging includes, like <iostream>.
//All inside is compiled once, to save time.
#include "pch.h"

//Note: .def file isn't necessary here due to dllexport prefixes.
//Otherwise, DLL needs that file with some text inside

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}

