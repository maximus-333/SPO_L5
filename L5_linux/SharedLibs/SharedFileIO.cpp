#include "SharedFileIO.h"

int fileReadDLL(aiocb &settings)
{
	aio_read(&settings);
	//Wait for completion
	while(EINPROGRESS == aio_error(&settings))
	{
		usleep(1000);
	}

	return aio_return(&settings);
}

int fileWriteDLL(aiocb &settings)
{
	aio_write(&settings);
	//Wait for completion
	while(EINPROGRESS == aio_error(&settings))
	{
		usleep(1000);
	}

	return aio_return(&settings);
}
