
//This is needed here(not in header file). Otherwise compiler freaks out
#include "pch.h"

#include "UserFunctions.h"
#include <iostream>

void tf() { std::cout << "Test Function!" << std::endl; }

int separatedFileRead(HANDLE fileHandle, char *buf, DWORD bufSize, DWORD *bytesProcessed, OVERLAPPED *asyncStruct)
{
	if (!ReadFile(fileHandle, buf, bufSize, bytesProcessed, asyncStruct))
		return GetLastError();
	else
		return 0;
}

int separatedFileWrite(HANDLE fileHandle, char *buf, DWORD bufSize, DWORD *bytesProcessed, OVERLAPPED *asyncStruct)
{
	if (!WriteFile(fileHandle, buf, bufSize, bytesProcessed, asyncStruct))
		return GetLastError();
	else
		return 0;
}