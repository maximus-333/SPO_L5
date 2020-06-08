


//Important lessons:
//- In DLL, use __declspec(dllexport) for explicit linking, and ...import for implicit linking(i guess that's how it works)
//- To avoid problems with function call, use extern "C" in DLL,
//  and __cdecl when making a function pointer. Otherwise - corrupted stack error


#include <iostream>
#include <dlfcn.h>	//for shared libs. IMPORTANT - also need to add "dl" library (-ldl)
#include <dirent.h>	//for directory searching in unix
#include <aio.h>	//Asynchronous IO

//All 3 for file operations
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <semaphore.h>
#include <sys/sem.h>
#include <unistd.h>
#include <sys/ipc.h>

#include <stack>
#include <string>
#include <memory>
#include <algorithm>


typedef int (*sharedFuncPtr)(aiocb&);


//POSIX semaphores to synchronize read/write btw threads
sem_t readSem;
sem_t writeSem;

const int BUF_SIZE = 1024;
int bytesToWrite = 0;
char readData[BUF_SIZE];






void* dllHandle;	//Handle for shared library


//Reader thread function. Format is needed for correct creation
void * readerThread(void* params) {

	DIR *dirPtr;
	dirent *dirEntry;
	std::stack<std::string> fileNameStack;

	dirPtr = opendir("./");
	if(dirPtr == NULL)
	{
		//Maybe better method?
		std::cerr << "Can't open current directory!" << std::endl;
		return 0;
	}
	//get all files, select .txt ones
	while(dirEntry = readdir(dirPtr))
	{
		if(dirEntry->d_type != DT_UNKNOWN && dirEntry->d_type != DT_REG)
		{
			continue;
		}

		std::string name(dirEntry->d_name);
		std::string txtType(".txt");
		//If file doesn't end with ".txt", don't include it.
		auto findIter = std::find_end(name.begin(), name.end(), txtType.begin(), txtType.end());
		if(findIter == name.end())
		{
			continue;
		}
		if(findIter != name.end() - txtType.length())
		{
			//If has ".txt", but not in the end, skip
			continue;
		}
		fileNameStack.push(name);
	}

	//Load a read function from shared lib
	char *loadRes;
	dlerror();	//Pointer can actually be NULL. To detect errors, use dlerror(). Reset it first
	sharedFuncPtr sharedReadPtr = (sharedFuncPtr)dlsym(dllHandle, "fileReadDLL");
	loadRes = dlerror();
	if(loadRes)
	{
		throw std::string("Error getting pointer to read function! Message: ") + std::string(loadRes);
	}

	//Open and read all .txt files
	while (!fileNameStack.empty())
	{
		//open file and such
		int fileReadHandle = open(fileNameStack.top().data(), O_RDONLY);
		if(fileReadHandle == -1)
		{
			if(errno == EACCES)
			{
				//Directory will have locked output file. Skip it and any other locked files
				fileNameStack.pop();
				continue;
			}

			std::cerr << "Can't open file!. Name: " << fileNameStack.top() << " Err code: " << errno << std::endl;
			return NULL;
		}

		//Struct for asynchronous file read
		aiocb aioRead = {0};
		aioRead.aio_fildes = fileReadHandle;
		aioRead.aio_buf = readData;
		aioRead.aio_nbytes = BUF_SIZE;
		aioRead.aio_sigevent.sigev_notify = SIGEV_NONE;


		//File opened. Now read it to main thread in small pieces, with synchronization
		while (1)
		{
			//Wait till main thread has finished with previous data
			sem_wait(&readSem);

			//Call function that has asynchronous operation inside with synchronization.
			//Will be put into DLL later.
			int readRes = sharedReadPtr(aioRead);
			if(readRes == -1)
				std::cerr << "Error reading file. Name: " << fileNameStack.top() << " Code: " << errno << std::endl;


			bytesToWrite = readRes;
			aioRead.aio_offset += readRes;

			//Signal main to write new data
			sem_post(&writeSem);

			if(!readRes)
				break;	//reached end of file, get new one
		}
		//Close this file, discard it, loop to next one

		close(fileReadHandle);

		fileNameStack.pop();
	}

	//Successfully fed all .txt files to main thread. Now cleanup and signal it to wrap up too

	bytesToWrite = -1; //Signal main thread

	//Unlock main so it can check for end value
	sem_post(&writeSem);

	return 0;
}


//Status - everything works. Now gonna copy and refurbish that for Linux.

int main()
{
	int fileWriteHandle;
	const char outputFileName[] = "output.txt";
	pthread_t readerThreadHandle;


	//Use try-catch block to properly close handles on any errors
	try {


		//Load DLL
		dllHandle = dlopen("./SharedLibs/libLab5_SharedLib.so", RTLD_LAZY);
		if(!dllHandle)
		{
			throw std::string("Error opening shared lib! Message: ") + std::string(dlerror());
		}



		//Create POSIX semaphores
		if(sem_init(&readSem, 0, 1))
		{
			throw std::string("Can't create read semaphore! Code: ") + std::to_string(errno);
		}
		if(sem_init(&writeSem, 0, 0))
		{
			throw std::string("Can't create write semaphore! Code: ") + std::to_string(errno);
		}


		//Open output file.
		fileWriteHandle = open(outputFileName,	O_WRONLY | O_APPEND | O_CREAT | O_TRUNC, 	S_IWUSR | S_IRWXO);
		if(fileWriteHandle == -1)
		{
			throw "Can't open output file! Code: " + std::to_string(errno);
		}


		//Get write function from shared lib
		char *loadRes;
		dlerror();	//Pointer can actually be NULL. To detect errors, use dlerror(). Reset it first
		sharedFuncPtr sharedWritePtr = (sharedFuncPtr)dlsym(dllHandle, "fileWriteDLL");
		loadRes = dlerror();
		if(loadRes)
		{
			throw std::string("Error getting pointer to write function! Message: ") + std::string(loadRes);
		}


		//Create thread
		if(int errnum = pthread_create(&readerThreadHandle, NULL, &readerThread, nullptr))
		{
			throw "Error creating reader thread. Code: " + std::to_string(errnum);
		}


		//Data for asynchronous writes
		aiocb writeSettings = {0};
		writeSettings.aio_buf = readData;
		writeSettings.aio_nbytes = BUF_SIZE;
		writeSettings.aio_fildes = fileWriteHandle;
		writeSettings.aio_sigevent.sigev_notify = SIGEV_NONE;

		//Cycle of writing data into output file
		while (1)
		{
			//Wait for data in buffer
			sem_wait(&writeSem);

			if (bytesToWrite == -1)
			{
				//No more data to read. Clean up and exit
				break;
			}

			//Need to update file offset?

			//Write data to output file
			writeSettings.aio_nbytes = bytesToWrite;
			int writeRes = sharedWritePtr(writeSettings);
			if(writeRes == -1)
			{
				throw std::string("Write failure. Code: ") + std::to_string(errno);
			}

			//Signal reader thread that he can continue
			sem_post(&readSem);

			//And go back to waiting for more data
		}
	}
	catch (std::string msg)
	{
		std::cerr << msg << std::endl;
	}

	//Cleanup should execute no matter the errors. Looks fine
	pthread_cancel(readerThreadHandle);
	//Closing output file handle
	chmod(outputFileName, S_IWOTH | S_IROTH | S_IRUSR | S_IWUSR);	//Permissions stay after release, so unlock the file for writing
	fsync(fileWriteHandle);
	close(fileWriteHandle);
	//Freeing semaphores(POSIX ones)
	sem_destroy(&readSem);
	sem_destroy(&writeSem);
	//Freeing shared library
	dlclose(dllHandle);

	std::cout << "Main thread is done. Everything cleaned" << std::endl;

	return 0;
}
