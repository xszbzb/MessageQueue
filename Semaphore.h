#pragma once

#include "SharedMemory.h"
#include <string.h>

#ifdef _WIN32

#include <windows.h>
#include <tlhelp32.h>

#define TIMEOUT_VARIABLE iRet
#define SEM_FAILED 0

#define sem_open(name, oflag, accessPermission, initValue) CreateSemaphoreA(CreateSecurityDescriptor(accessPermission), initValue, 1, name)
#define TimeWait(semaphore, timeout) WaitForSingleObject(semaphore, timeout)
#define sem_wait(semaphore) WaitForSingleObject(semaphore, INFINITE)
#define sem_post(semaphore) (!ReleaseSemaphore(semaphore, 1, NULL))
#define sem_close(semaphore) CloseHandle(semaphore)

static bool QueryThreadExist(DWORD ThreadID)
{
	if (0 == ThreadID)return false;
	HANDLE h_Process = OpenThread(THREAD_QUERY_INFORMATION, FALSE, ThreadID);
	if (!h_Process)
	{
		DWORD error = GetLastError();
		if (error == ERROR_SUCCESS || ERROR_ACCESS_DENIED == error)
		{
			//LOG_DEBUG("Thread exist\r\n");
			return true;
		}
		else
		{
			//LOG_DEBUG("Thread does not exist, error:%d\r\n", error);
			return false;
		}
	}

	DWORD dwExitCode = 0;
	BOOL ret = GetExitCodeThread(h_Process, &dwExitCode);
	CloseHandle(h_Process);
	if (0 == ret || (0 != ret && STILL_ACTIVE != dwExitCode))
		return false;
	return true;
}

#else

#include <stdio.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/syscall.h>

#define GetCurrentThreadId() syscall(__NR_gettid)
#define WAIT_OBJECT_0 0
#define WAIT_FAILED -1
#define WAIT_TIMEOUT ETIMEDOUT
#define TIMEOUT_VARIABLE errno

static int TimeWait(sem_t *semaphore, unsigned long timeoutMilliseconds)
{
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	ts.tv_nsec += timeoutMilliseconds % 1000 * 1000000;
	ts.tv_sec += timeoutMilliseconds / 1000 + ts.tv_nsec/1000000000;
	ts.tv_nsec %= 1000000000;
	return sem_timedwait(semaphore, &ts);
}

static bool QueryThreadExist(int ThreadID)
{
	if (0 == ThreadID)return false;
	char buf[MAX_SHARED_MEMORY_NAME_SIZE] = { 0 };
	sprintf(buf, "/proc/%d/exe", ThreadID);
	FILE *fp = fopen(buf, "r");
	if (fp)
	{
		//LOG_DEBUG("Thread exist\r\n");
		fclose(fp);
		return true;
	}
	else
	{
		//LOG_DEBUG("Thread does not exist, error:%d\r\n", error);
		return false;
	}
}

#endif


class CSemaphore
{
public:

	CSemaphore(const unsigned int key, const unsigned int initValue = 1, const unsigned int accessPermission = 0666);
	//CSemaphore(const char* nameSem, const unsigned int initValue = 1, const unsigned int accessPermission = 0666);
	virtual ~CSemaphore();
	virtual int Wait(unsigned long timeoutMilliseconds = (unsigned long)-1);
	virtual int Post();

private:
	static const int INTERVAL_TIMEOUT = 1000; //ºÁÃë
	sem_t *m_sem;
	unsigned int *m_pThreadIDSharedMemory;
};

inline CSemaphore::CSemaphore(const unsigned int key, const unsigned int initValue, const unsigned int accessPermission)
{
	m_pThreadIDSharedMemory = NULL;
	m_sem = NULL;
	char nameSem[MAX_SHARED_MEMORY_NAME_SIZE] = { 0 };
	sprintf(nameSem, "Global\\Semaphore_%u", key);
	m_sem = sem_open(nameSem, O_CREAT, accessPermission, initValue);
	if (SEM_FAILED == m_sem)
	{
		LOG_ERROR("Semaphore application failed, key=%u, error=%s\r\n", key, GetErrorStr(ERRNO));
		exit(ERRNO);
	}
#ifdef __linux__
	sprintf(nameSem, "/dev/shm/sem.Global\\Semaphore_%u", key);
	chmod(nameSem, accessPermission);
#endif
	CSharedMemory sharedMemory(key, sizeof(int), accessPermission);
	m_pThreadIDSharedMemory = (unsigned int*)sharedMemory.Begin();
}

//CSemaphore::CSemaphore(const char* nameSem, const unsigned int initValue, const unsigned int accessPermission)
//{
//	m_sem = sem_open(nameSem, O_CREAT, accessPermission, initValue);
//	if (SEM_FAILED == m_sem)
//	{
//		LOG_ERROR("Semaphore application failed\r\n");
//		exit(-2);
//	}
//}

inline CSemaphore::~CSemaphore()
{
#ifdef SHARED_MEMORY_CLEAR
	if (NULL != m_sem)
		sem_close(m_sem);
#endif
}

inline int CSemaphore::Wait(unsigned long timeoutMilliseconds)
{
	int iRet = WAIT_OBJECT_0;
	unsigned long uInterval = 0;
	while (NULL == m_pThreadIDSharedMemory)Sleep(1);

	while(1)
	{
		if (timeoutMilliseconds == (unsigned long)-1)
		{
			uInterval = INTERVAL_TIMEOUT;
		}
		else if (timeoutMilliseconds > INTERVAL_TIMEOUT)
		{
			timeoutMilliseconds -= INTERVAL_TIMEOUT;
			uInterval = INTERVAL_TIMEOUT;
		}
		else
		{
			uInterval = timeoutMilliseconds;
			timeoutMilliseconds = 0;
		}
		iRet = TimeWait(m_sem, uInterval);
		if (iRet == WAIT_OBJECT_0)
		{
			*m_pThreadIDSharedMemory = GetCurrentThreadId();
			return WAIT_OBJECT_0;
		}
		switch (TIMEOUT_VARIABLE)
		{
		case WAIT_TIMEOUT:
			if (*m_pThreadIDSharedMemory == GetCurrentThreadId())
			{
				if (0 == timeoutMilliseconds)
					return WAIT_TIMEOUT;
				else
					continue;
			}
			if (!QueryThreadExist(*m_pThreadIDSharedMemory))
			{
				*m_pThreadIDSharedMemory = GetCurrentThreadId();
				Sleep(1);
				if (*m_pThreadIDSharedMemory == GetCurrentThreadId())
				{
					LOG_DEBUG("The semaphore is abnormally locked and unlocked.\r\n");
					return WAIT_OBJECT_0;
				}
			}
			if (0 == timeoutMilliseconds)
				return WAIT_TIMEOUT;
			break;
		case WAIT_FAILED:
			LOG_ERROR("Semaphore WAIT_FAILED error, error=%s\r\n", GetErrorStr(ERRNO));
			return TIMEOUT_VARIABLE;
		default:
			LOG_ERROR("Semaphore other error, error=%s\r\n", GetErrorStr(ERRNO));
			return TIMEOUT_VARIABLE;
		}
	}
}

inline int CSemaphore::Post()
{
	if (NULL == m_sem)
		return 0;
#ifdef _WIN32
	sem_post(m_sem);
	return 0;
#else
	int iRet = sem_post(m_sem);
	if (iRet == WAIT_OBJECT_0)return WAIT_OBJECT_0;
	LOG_ERROR("Semaphore error, error=%s\r\n", GetErrorStr(ERRNO));
	return ERRNO;
#endif
}