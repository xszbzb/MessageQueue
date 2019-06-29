//linux编译测试，加1个参数与不加参数来区分读写队列
//g++ -o t -D MESSAGE_QUEUE_TRACE MessageQueueMain.cpp -lpthread && ./t -r

//清理消息队列编译命令
//g++ -o t -D MESSAGE_QUEUE_TRACE -D MESSAGE_QUEUE_CLEAR MessageQueueMain.cpp -lpthread && ./t
//#define MESSAGE_QUEUE_TRACE 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "MessageQueue.h"

#if 1
int main(int argc, char* argv[])
{
	char name[CMessageQueue::MAX_MESSAGE_SIZE] = { 0 };
	char recv[CMessageQueue::MAX_MESSAGE_SIZE] = { 0 };
	sprintf(name, "argc_%d_0123456789_0123456789_0123456789_0123456789_0123456789_0123456789_0123456789_0123456789_0123456789_0123456789_0123456789_0123456789_0123456789_0123456789_\r\n", argc);
	CMessageQueue messageQueue(897654321, 0x1000);
	//messageQueue.SetMode(CMessageQueue::ASYNCHRONOUS);
#ifndef MESSAGE_QUEUE_CLEAR
	unsigned int iRet = 0;
	if (argc & 1)
	{
		printf("Write message\r\n");
		for (int i = 1; i <= 10000000; i++)
		{
			if (0 > (iRet = messageQueue.Write(name, CMessageQueue::MAX_MESSAGE_SIZE - 333 + i % 333)))
			{
				printf("Write message error\r\n");
			}
			else if (iRet == 0)
			{
				Sleep(1000);
				printf("Write message failed\r\n");
			}
			if (i % 1000000 == 0)
				printf("Write message: %u\r\n", i);
		}
	}
	else
	{
		printf("Read message\r\n");
		for (int i = 1; ; i++)
		{
			if (0 > (iRet = messageQueue.Read(recv, CMessageQueue::MAX_MESSAGE_SIZE)))
			{
				printf("Read message error\r\n");
			}
			else if (iRet == 0)
			{
				Sleep(1000);
				printf("Read message failed\r\n");
			}
			if(i%1000000 == 0)
				printf("Read message: %u\r\n", i);
		}
	}
#endif
    return 0;
}
#endif


//1个进程读,1个进程写
//windows 1000万条 19秒
//linux 1000万条 3秒
//1个进程读,2个进程写
//windows 2000万条 80秒
//linux 2000万条 23秒
//2个进程读,2个进程写
//windows 2000万条 96秒
//linux 2000万条 23秒

//4个信号量
//1个进程读,1个进程写
//windows 1000万条 35秒*
//linux 1000万条 3秒
//1个进程读,2个进程写
//windows 2000万条 80秒
//linux 2000万条 23秒
//2个进程读,2个进程写
//windows 2000万条 140秒*
//linux 2000万条 23秒


#if 0
int main(int argc, char* argv[])
{
	CSemaphore sem(789789);
	for (int i = 0; i < 10000000; i++)
	{
		if (argc & 1)
		{
			printf("Wait\n");
			sem.Wait();
		}
		else
		{
			printf("Post\n");
			sem.Post();
			Sleep(1000);
		}
	}
	return 0;
}
#endif