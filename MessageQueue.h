#pragma once

#ifdef MESSAGE_QUEUE_TRACE
#define PRINT_FREQUENCY 1000000
#endif

#include "Semaphore.h"

//线程宏
#ifdef _WIN32
#define pthread_t HANDLE
#define pthread_create(pntid, NULL, thread_func, param) \
	(\
	*pntid = CreateThread(NULL, 0, thread_func, param, 0, NULL),\
	CloseHandle(*pntid),\
	(*pntid == 0) ? -1 : 0\
	);

#define THREAD_FUNC_RETURN_TYPE DWORD WINAPI
#else
#define THREAD_FUNC_RETURN_TYPE void*
#endif

class CMessageQueue
{
public:
	static const unsigned int MAX_MESSAGE_SIZE = 0X1000 - sizeof(unsigned int);

	enum E_MODE_BLOCK
	{
		ASYNCHRONOUS,
		BLOCK
	};

	struct CMessageHead
	{
		unsigned int len;
		char msg[MAX_MESSAGE_SIZE];
	};

	struct CSharedMemoryHead
	{
		unsigned int size; 
		unsigned int head;
		unsigned int tail;
		unsigned int read;
		unsigned int write;
		unsigned int reserved[2]; //预留
		CMessageHead data[];
	};

	CMessageQueue(const unsigned int key = 897654321, const unsigned int queueSize = 0x100, const unsigned int accessPermission = 0666);
	virtual ~CMessageQueue();
	//参数:要写入消息队列的数据和长度
	//成功返回>0,失败返回<0,==0代表异步时,队列满了不能再写入
	virtual int Write(const void* buf, unsigned int len);
	//buf:接收数据指针
	//len:可接收的最大数据长度
	//成功返回读取数据长度>0,失败返回小于0,==0代表异步时,队列中无数据可读
	virtual int Read(void* const buf, unsigned int len);
	virtual void Clear();
	virtual E_MODE_BLOCK SetMode(E_MODE_BLOCK mode);

protected:
	bool IsWrite();
	bool IsRead();

private:
	unsigned int m_key;
	CSemaphore m_semRead;
	CSemaphore m_semWrite;
	CSemaphore m_semWaitRead;//队列写满时等待读取消息
	CSemaphore m_semWaitWrite;//队列为空时等待写入新消息
	CSharedMemoryHead* m_pBase;//分配的共享内存的原始首地址
	unsigned int m_queueSize;
	E_MODE_BLOCK m_modeBlock;
};


inline CMessageQueue::CMessageQueue(const unsigned int key, const unsigned int queueSize, const unsigned int accessPermission):m_key(key*10),
m_semRead(++m_key), m_semWrite(++m_key), m_semWaitRead(++m_key), m_semWaitWrite(++m_key)
{
	m_modeBlock = BLOCK;
	m_queueSize = queueSize;
	size_t size = sizeof(CSharedMemoryHead) + sizeof(CMessageHead)*m_queueSize;
	static CSemaphore sem(0xFFFFFFFF, 1, 0666);
	sem.Wait();
	CSharedMemory sharedMemory(key, size, accessPermission);
	m_pBase = (CSharedMemoryHead*)sharedMemory.Begin();
	if (m_pBase->size == 0)
	{
		m_pBase->size = size;
	}
	else
	{
		if (m_pBase->size != size)
		{
			sem.Post();
			LOG_ERROR("Message queue size does not match, key=%u, error=%s\r\n", key, GetErrorStr(ERRNO));
			exit(ERRNO);
		}
	}
	sem.Post();
}

inline CMessageQueue::~CMessageQueue()
{
}

inline bool CMessageQueue::IsWrite()
{
	return ((m_pBase->head + 1) % m_queueSize) != m_pBase->tail;
}

inline int CMessageQueue::Write(const void* buf, unsigned int len)
{
	if (m_modeBlock == ASYNCHRONOUS && !IsWrite())
		return 0;

	if (len > MAX_MESSAGE_SIZE || len == 0)
	{
		LOG_ERROR("Message length error, len=%u, Max length=%u\r\n", len, MAX_MESSAGE_SIZE);
		return -1;
	}
	
	int iRet = 0;
	if (WAIT_OBJECT_0 == (iRet = m_semWrite.Wait()))
	{
		while (!IsWrite())
		{
			if (WAIT_OBJECT_0 != (iRet = m_semWaitRead.Wait()))
			{
				LOG_ERROR("Semaphore error: error=%u\r\n", iRet);
				return -1;
			}
		}
	}
	else
	{
		LOG_ERROR("Semaphore error: error=%u\r\n", iRet);
		return -1;
	}

	m_pBase->data[m_pBase->head].len = len;
	memcpy(m_pBase->data[m_pBase->head].msg, buf, len);

#ifdef MESSAGE_QUEUE_TRACE
	unsigned int write = m_pBase->write;
	m_pBase->write++;
	if (m_pBase->write % PRINT_FREQUENCY == 0)LOG_TRACE("---------write:%u\r\n", m_pBase->write);
	if (m_pBase->write != write + 1)
	{
		m_semWrite.Post();
		LOG_ERROR("Message queue error:m_pBase->write:%u, write=%u\r\n", m_pBase->write, write);
		return -1;
	}
#endif

	m_pBase->head < m_queueSize - 1 ? m_pBase->head++ : m_pBase->head = 0;
	m_semWaitWrite.Post();
	m_semWrite.Post();
	return len;
}

inline bool CMessageQueue::IsRead()
{
	return m_pBase->tail != m_pBase->head;
}

inline int CMessageQueue::Read(void* const buf, unsigned int len)
{
	if (m_modeBlock == ASYNCHRONOUS && !IsRead())
		return 0;

	int iRet = 0;
	if (WAIT_OBJECT_0 == (iRet = m_semRead.Wait()))
	{
		while (!IsRead())
		{
			if (WAIT_OBJECT_0 != (iRet = m_semWaitWrite.Wait()))
			{
				LOG_ERROR("Semaphore error: error=%u\r\n", iRet);
				return -1;
			}
		}
	}
	else
	{
		LOG_ERROR("Semaphore error: error=%u\r\n", iRet);
		return -1;
	}

	if (len < m_pBase->data[m_pBase->tail].len)
	{
		m_semRead.Post();
		LOG_ERROR("Message length error, Maximum acceptable length=%u, Current message length=%u\r\n", len, ((CMessageHead*)&m_pBase->data[m_pBase->tail])->len);
		return -1;
	}

	len = m_pBase->data[m_pBase->tail].len;
	memcpy(buf, m_pBase->data[m_pBase->tail].msg, len);

#ifdef MESSAGE_QUEUE_TRACE
	unsigned int read = m_pBase->read;
	m_pBase->read++;
	if (m_pBase->read % PRINT_FREQUENCY == 0)LOG_TRACE("---------read:%u\r\n", m_pBase->read);
	if (m_pBase->read != read + 1)
	{
		m_semRead.Post();
		LOG_ERROR("Message queue error:m_pBase->read:%u, read=%u\r\n", m_pBase->read, read);
		return -1;
	}
#endif

	m_pBase->tail < m_queueSize - 1 ? m_pBase->tail++ : m_pBase->tail = 0;
	m_semWaitRead.Post();
	m_semRead.Post();
	return len;
}

inline void CMessageQueue::Clear()
{
	m_semRead.Wait();
	m_pBase->tail = m_pBase->head;
	m_semWaitRead.Post();
	m_semRead.Post();
}

inline CMessageQueue::E_MODE_BLOCK CMessageQueue::SetMode(E_MODE_BLOCK mode)
{
	mode == ASYNCHRONOUS ? m_modeBlock = ASYNCHRONOUS : m_modeBlock = BLOCK;
	return m_modeBlock;
}
