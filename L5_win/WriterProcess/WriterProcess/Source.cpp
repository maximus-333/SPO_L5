


//Important lessons:
//- In DLL, use __declspec(dllexport) for explicit linking, and ...import for implicit linking(i guess that's how it works)
//- To avoid problems with function call, use extern "C" in DLL, 
//  and __cdecl when making a function pointer. Otherwise - corrupted stack error


#include <iostream>
#include <Windows.h>

#include <stack>
#include <string>


HANDLE readEvent;	//file read complete
HANDLE writeEvent;	//file write complete

const int BUF_SIZE = 1024;
DWORD bytesToWrite = 0;
char readData[BUF_SIZE];

HMODULE dllHandle;	//Global to share DLL with reader thread - bad to open it twice
//Function ptr prototype. Needed to use function ptrs from DLL. Idk if CALLBACK is needed
typedef int (__cdecl *DLLFUNCPTR)(HANDLE, char*, DWORD, DWORD*, OVERLAPPED*);


//Reader thread function. Format is needed for correct creation
DWORD WINAPI readerThread(LPVOID params) {
	HANDLE searchResults;
	WIN32_FIND_DATAA fileInfo;
	searchResults = FindFirstFileA(".\\*.txt", &fileInfo);
	if (searchResults == INVALID_HANDLE_VALUE)
	{
		//Found no .txt files in directory. 
		//Unexpected, there should at least be output file created in main thread
		std::cerr << "No .txt files found(or error)! Weird... Have error code anyway: " << GetLastError() << std::endl;
	}



	//Do all search together, and form a list of .txt file names
	//(also feature - can get file size right away. Not that useful here)
	std::stack<std::string> fileNames;
	fileNames.push(fileInfo.cFileName);
	while (1)
	{
		if (!FindNextFileA(searchResults, &fileInfo))
		{
			//found all .txt files
			break;
		}
		fileNames.push(fileInfo.cFileName);
	}
	//Found all .txt files. Now read them and feed their data to main thread


	//Load a read function from DLL
	DLLFUNCPTR readFuncPtr;
	readFuncPtr = (DLLFUNCPTR)GetProcAddress(dllHandle, "separatedFileRead");
	if (!readFuncPtr)
	{
		throw("Error retrieving read function from DLL! Error code: " + std::to_string(GetLastError()));
	}


	while (!fileNames.empty())
	{
		std::string fullFilePath = ".\\" + fileNames.top();
		HANDLE currFile = CreateFileA(fullFilePath.data(), GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		if (currFile == INVALID_HANDLE_VALUE)
		{
			if (GetLastError() != ERROR_SHARING_VIOLATION)	//Output file should give this error on attempt to open it. Ignore
			{
				std::cerr << "Unexpected error opening file for read. File name: " << fileNames.top().data() 
					<< "; Error code: " << GetLastError() << std::endl;
			}
		}
		else
		{
			//Struct for asynchronous file read
			OVERLAPPED asyncEvent = { 0 };
			asyncEvent.hEvent = readEvent;


			//File opened. Now read it to main thread in small pieces, with synchronization
			while (1)
			{
				//Wait till main thread has finished with previous data
				WaitForSingleObject(writeEvent, INFINITE);

				//Manually move to next chunk of data in file
				asyncEvent.Offset += bytesToWrite;

				//stack corrupted by +20 bytes. Find how to fix
				//now corrupted by  bytes

				//ReadFile(currFile, &readData, BUF_SIZE, &bytesToWrite, &asyncEvent)
				if (int errc = readFuncPtr(currFile, readData, BUF_SIZE, &bytesToWrite, &asyncEvent))
				{
					if (errc == ERROR_HANDLE_EOF)
					{
						//read the entire file. Go to next one
						break;
					}

					std::cerr << "Error reading data from file. File name: " << fileNames.top().data()
						<< "; Error code: " << errc << std::endl;
				}

				//Have to wait for correct data. Still faster than synchronous
				//GetOverlappedResult(currFile, &asyncEvent, &bytesToWrite, TRUE);
			}
		}
		//Close this file, discard it, loop to next one
		CloseHandle(currFile);
		fileNames.pop();
	}
	
	//Successfully fed all .txt files to main thread. Now cleanup and signal it to wrap up too

	FindClose(searchResults);
	
	bytesToWrite = -1; //Signal main thread
	SetEvent(readEvent);

	return 0;
}


//Status - everything works. Now gonna copy and refurbish that for Linux.

int main()
{
	HANDLE outputFile = INVALID_HANDLE_VALUE;
	HANDLE threadHandle = INVALID_HANDLE_VALUE;

	//Use try-catch block to properly close handles on any errors
	try {

		//Load DLL with functions for file IO
		dllHandle = LoadLibraryA("ReadWriteDLL.dll");
		if (dllHandle == NULL)
		{
			throw("Failed to explicitly link a DLL. Error code: " + std::to_string(GetLastError()));
		}
		

		//Create events
		readEvent = CreateEvent(NULL, false, 0, "L5_readEvent");
		if (!readEvent)
		{
			throw("Error creating read event! Code: " + std::to_string(GetLastError()));
		}
		writeEvent = CreateEvent(NULL, false, 1, "L5_writeEvent");
		if (!writeEvent)
		{
			throw("Error creating write event! Code: " + std::to_string(GetLastError()));
		}
		//(SetEvent(), ResetEvent())


		//Create output file
		//(for write only, with lock)
		outputFile = CreateFile(".\\output.txt", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		if (outputFile == INVALID_HANDLE_VALUE)
		{
			throw("Error creating output file! Code: " + std::to_string(GetLastError()));
		}


		//Creating thread
		threadHandle = CreateThread(NULL, 0, readerThread, nullptr, 0, nullptr);
		if (!threadHandle)
		{
			throw("Error creating thread! Code: " + std::to_string(GetLastError()));
		}

		//Struct for asynchronous file operation. Need to manually update offset
		OVERLAPPED asyncStruct = { 0 };
		asyncStruct.hEvent = writeEvent;	//resume readerThread right after write
		DWORD bytesWritten = 0;		//bytes written to output file

		//Load a write function from DLL
		DLLFUNCPTR writeFuncPtr = 0;
		writeFuncPtr = (DLLFUNCPTR)GetProcAddress(dllHandle, "separatedFileWrite");
		if (!writeFuncPtr)
		{
			throw("Error retrieving write function from DLL! Error code: " + std::to_string(GetLastError()));
		}

		//Cycle of writing data into output file
		while (1)
		{
			//Wait till reader thread got data
			if (WaitForSingleObject(readEvent, INFINITE))
			{
				throw("Error on wait for read! Code: " + std::to_string(GetLastError()));
			}

			if (bytesToWrite == -1)
			{
				//No more data to read. Clean up and exit
				break;
			}

			//Need to update file offset. Unlikely to have offset more than 2^32, so forget about offsetHigh
			asyncStruct.Offset += bytesWritten;


			//Write data to output
			//WriteFile(outputFile, &readData, bytesToWrite, &bytesWritten, &asyncStruct)

			//PROBLEM - stack corruption. Idk how to fix, gonna try

			if (int errc = writeFuncPtr(outputFile, readData, bytesToWrite, &bytesWritten, &asyncStruct))
			{
				throw("(test) Error on output to file! Code: " + std::to_string(errc));
			}
			if (bytesToWrite != bytesWritten)
			{
				throw (std::string("Didn't write all of bytes to output. Weird."));
			}
			
			std::cerr << "wrote " << bytesWritten << " bytes to file" << std::endl;


			//Signal reader thread that he can continue
			//SetEvent(writeEvent);
			//And go back to waiting for more data
		}
	}
	catch (std::string msg)
	{
		std::cerr << msg << std::endl;
	}

	//Cleanup should execute no matter the errors. Looks fine
	CloseHandle(outputFile);
	TerminateThread(threadHandle, 0);	//idk if this is needed
	CloseHandle(threadHandle);			//maybe this isn't needed too

	FreeLibrary(dllHandle);

	std::cout << "Main thread is done. Everything cleaned" << std::endl;
	
	return 0;
}