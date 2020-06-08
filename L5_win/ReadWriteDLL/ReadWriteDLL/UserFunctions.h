#pragma once

#include <Windows.h>

//Project template declares ..._EXPORTS(it's preprocessor definition).
//Idk what is going on exactly, but 1st option is used(and needed) for DLL,
//other is needed for implicit linking, i guess
#ifdef READWRITEDLL_EXPORTS
#define DLLMOD __declspec(dllexport)
#else
#define DLLMOD __declspec(dllimport)
#endif


//Important(?) note: fnctions seem to start working when that fancy DLLMOD define if replaced with dllexport thing


//This is a calling convention for public functions.
	extern"C" __declspec(dllexport) void tf();

	extern"C" __declspec(dllexport) int separatedFileRead(HANDLE fileHandle, char *buf, DWORD bufSize, DWORD *bytesProcessed, OVERLAPPED *asyncStruct);

	extern"C" __declspec(dllexport) int separatedFileWrite(HANDLE fileHandle, char *buf, DWORD bufSize, DWORD *bytesProcessed, OVERLAPPED *asyncStruct);

