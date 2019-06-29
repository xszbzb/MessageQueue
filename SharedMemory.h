#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <iostream>

#define LOG_ERROR(fmt, ...) printf("[E][%s][%s][%d]:" fmt , __FILE__, __FUNCTION__, __LINE__,##__VA_ARGS__)
#define LOG_DEBUG(fmt, ...) printf("[D][%s][%s][%d]:" fmt , __FILE__, __FUNCTION__, __LINE__,##__VA_ARGS__)

#ifdef MESSAGE_QUEUE_TRACE
#define LOG_TRACE(fmt, ...) printf("[T][%s][%s][%d]:" fmt , __FILE__, __FUNCTION__, __LINE__,##__VA_ARGS__)
#define LOG_POINT LOG_DEBUG("***************************\r\n");
#else
#define LOG_TRACE(fmt, ...)
#define LOG_POINT
#endif

#define MAX_SHARED_MEMORY_NAME_SIZE 0X100

#ifdef _WIN32

#include <windows.h>
#include <process.h>
#include <sddl.h>

#define sprintf sprintf_s
#define shmdt UnmapViewOfFile
#define sem_t void
#define O_CREAT 0
#define O_EXCL 1
#define ERRNO GetLastError()

static char* GetErrorStr(int errorCode)
{
	static char buf[0x100];
	buf[0] = 0;
	FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM,
		NULL,
		errorCode,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		buf,
		0x100,
		NULL);
	return buf;
}

static SECURITY_ATTRIBUTES* CreateSecurityDescriptor(const unsigned int accessPermission)
{
	static SECURITY_ATTRIBUTES security = {0};
	if (0 == security.nLength)
	{
		security.nLength = sizeof(security);
		const char* sharessdl = "D:P(A;;GA;;;WD)";
		BOOL bRet = ConvertStringSecurityDescriptorToSecurityDescriptorA(sharessdl, SDDL_REVISION_1, &security.lpSecurityDescriptor, NULL);
		if (0 == bRet)
		{
			LOG_ERROR("Create security descriptor failed\r\n");
			exit(-1);
		}
		//LocalFree(security.lpSecurityDescriptor);
		//security.lpSecurityDescriptor = NULL;
	}
	return &security;
}

#else

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <errno.h>
#include <semaphore.h>
#include <unistd.h>
#include <fcntl.h>

#define ERRNO errno
#define Sleep(time) usleep(time*1000)
#define GetErrorStr strerror

#endif


class CSharedMemory
{
public:
	//参数:	key是唯一标示
	//		size是共享内存大小
	//		accessPermission是访问权限
	CSharedMemory(const unsigned int key, const unsigned int size, const unsigned int accessPermission = 0666);
	virtual ~CSharedMemory();
	//返回共享内存首地址
	virtual void* Begin();

private:
#ifdef _WIN32
	sem_t *m_shared;
#else
	int m_shmid;
#endif
	void* m_pBase;//分配的共享内存的原始首地址
};

inline CSharedMemory::~CSharedMemory()
{
#ifdef SHARED_MEMORY_CLEAR
	//解除文件映射
	if (NULL != m_pBase)
		shmdt(m_pBase);
#ifdef _WIN32
	//关闭内存映射文件对象句柄
	if (NULL != m_shared)
		CloseHandle(m_shared);
#else
	//删除共享内存
	if (-1 != m_shmid)
		shmctl(m_shmid, IPC_RMID, NULL);
#endif
#endif
}

inline void* CSharedMemory::Begin()
{
	return m_pBase;
}

#ifdef _WIN32
inline CSharedMemory::CSharedMemory(const unsigned int key, const unsigned int size, const unsigned int accessPermission)
{
	char nameHead[MAX_SHARED_MEMORY_NAME_SIZE] = { 0 };
	sprintf(nameHead, "Global\\SharedMemory_%u", key);

	//创建共享文件句柄
	m_shared = CreateFileMappingA(
		INVALID_HANDLE_VALUE,//物理文件句柄
		CreateSecurityDescriptor(accessPermission),//安全级别
		PAGE_READWRITE,//可读可写
		0,//高位文件大小
		size,//共享内存大小
		nameHead//共享内存名称
	);

	int iRet = GetLastError();
	if (NULL == m_shared && ERROR_ACCESS_DENIED == iRet)
	{
		LOG_DEBUG("name=%s, Failed to create global shared memory, Try to create a non-global shared memory\r\n", nameHead);
		sprintf(nameHead, "MessageQueue_%u", key);
		m_shared = CreateFileMappingA(
			INVALID_HANDLE_VALUE,//物理文件句柄
			CreateSecurityDescriptor(accessPermission),//安全级别
			PAGE_READWRITE,//可读可写
			0,//高位文件大小
			size,//共享内存大小
			nameHead//共享内存名称
		);
	}

	iRet = GetLastError();
	if (NULL == m_shared)
	{
		LOG_ERROR("name=%s, Create shared memory failed, key=%u, error=%s\r\n", nameHead, key, GetErrorStr(ERRNO));
		exit(ERRNO);
	}

	m_pBase = (void*)MapViewOfFile(
		m_shared,//共享内存的句柄
		FILE_MAP_ALL_ACCESS,//可读写许可
		0,
		0,
		size
	);
	if (NULL == m_pBase)
	{
		LOG_ERROR("name=%s, Get shared memory point failed, key=%u, error=%s\r\n", nameHead, key, GetErrorStr(ERRNO));
		exit(ERRNO);
	}

	LOG_DEBUG("name=%s, Open shared memory successful\r\n", nameHead);
	if (ERROR_ALREADY_EXISTS != iRet)
	{
		memset(m_pBase, 0, size);
		LOG_DEBUG("name=%s, Init shared memory\r\n", nameHead);
	}
}
#else
inline CSharedMemory::CSharedMemory(const unsigned int key, const unsigned int size, const unsigned int accessPermission)
{
	if (0 == key)
	{
		LOG_ERROR("key=%u, Create shared memory failed\r\n", key);
		exit(-1);
	}
	bool exist = false;
	//创建共享内存
	m_shmid = shmget((key_t)key, size, accessPermission | IPC_CREAT | IPC_EXCL);
	LOG_TRACE("Shared memory create, error=%s\r\n", GetErrorStr(ERRNO));
	if (errno == EEXIST)
	{
		exist = true;
		LOG_DEBUG("Shared memory already exists\r\n");
		m_shmid = shmget((key_t)key, size, accessPermission | IPC_CREAT);
	}
	LOG_TRACE("m_shmid=%d\r\n", m_shmid);
	if (m_shmid == -1)
	{
		LOG_ERROR("shmget failed, key=%u, error=%s\r\n", key, GetErrorStr(ERRNO));
		exit(ERRNO);
	}
	//将共享内存连接到当前进程的地址空间
	m_pBase = (void*)shmat(m_shmid, 0, 0);
	if (m_pBase == (void*)-1)
	{
		LOG_ERROR("shmat failed, key=%u, error=%s\r\n", key, GetErrorStr(ERRNO));
		exit(ERRNO);
	}

	if (!exist)
	{
		memset(m_pBase, 0, size);
		LOG_DEBUG("key=%u, Init shared memory\r\n", key);
	}
}
#endif

