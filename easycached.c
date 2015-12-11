//easycached 缓存服务器启动
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <pthread.h>
#include "slab.h"

//函数的返回值类型
#define SUCCESS 1
#define FAILED 0
#define EXIT_SUCCESS 0
#define EXIT_FAILED 1

//内存单位
#define B 1
#define KB 1024 * B
#define MB 1024 * KB

//BOOL 类型
#define BOOL int
#define TRUE 1
#define FALSE 0

struct Config
{
	int nMemCachedSize; /*MB为单位*/
	char *sSockIp;//套接字IP 
	int nPort;//监听端口
	int nSockType;//1-tcp 2-udp
	int nThreadNumber;//处理任务的线程数
	BOOL bDaemonize;//守护进程
	int nChunkSize;//每一个块放入的数据大小
	BOOL bSysCore;//修改进程的资源限制
	BOOL bLog;//打开运行日志
	int nPageSize;//每一个页的大小
	float fFactor;//增长因子
};

typedef struct Config Config;
static Config config;

struct Connection
{
	int nFd;
	Connection *pNext;
};

struct Thread 
{
	pthread_t threadId;
	BOOL bExit;
	Connection *pConnQueue;
	Connection *pTail;
	pthread_mutex_t queueMutex;
};

typedef struct Thread Thread;
typedef struct Connection Connection;
static Thread *pThreadArray = NULL;
static int nLoadBalanceIndex = 0;

//确保所有线程已初始化
static pthread_mutex_t init_thread_mutex;
static int nThreadCount = 0;

static void DefaultConfig()
{
	//默认初始化
	config.nMemCachedSize = 64;
	config.sSockIp = INDRR_ANY;
	config.nPort = 6688;
	config.nSockType = 1;
	config.nThreadNumber = 4;
	config.bDaemonize = FALSE;
	config.nChunkSize = 40;
	config.bSysCore = FALSE;
	config.nMaxConn = 1024;
	config.bLog = TRUE;
	config.nPageSize = ALIGN_MEM(48);
	config.fFactor = 1.25;
}

static void InitConfig(int argc, char **argv)
{
	DefaultConfig();
	
	int nOpt = 0;
	while ((nOpt = getopt(argc, argv, 
			"m:"//分配的内存大小
			"i:"//socket ip
			"p:"//端口号
			"s:"//socket 类型 1-tcp 2-udp
			"t:"//处理任务线程数
			"d"//是否运行为守护进程
			"c:"//块大小
			"o"//修改进程资源限制
			"n"//最大连接数
			"l"//关闭日志
			"P"//每页大小
			"f"//增长因子
			)) != -1)
	{
		switch (nOpt)
		{
			case 'm':
				config.nMemCachedSize = atoi(optarg);
				if (config.nMemCachedSize <= 0)
				{
					fprintf(stderr, "选项%c的参数是无效的参数\n", nOpt);
					exit(EXIT_SUCCESS);
				}
				break;
				
			case 'i':
				config.sSockIp = strdup(optarg);
				if (!config.sSockIp)
				{
					fprintf(stderr, "选项%c的参数是无效的参数\n", nOpt);
					exit(EXIT_SUCCESS);
				}
				break;
				
			case 'p':
				config.nPort = atoi(optarg);
				break;
				
			case 's':
				config.nSockType = atoi(optarg);
				break;
				
			case 't':
				config.nThreadNumber = atoi(optarg);
				break;
				
			case 'd':
				config.bDaemonize = ((atoi(optarg) > 0) ? TRUE : FALSE);
				break;
				
			case 'c':
				config.nChunkSize = atoi(optarg);
				break;
				
			case 'o':
				config.bSysCore = TRUE;
				break;
				
			case 'n':
				config.nMaxConn = atoi(optarg);
				break;
				
			case 'l':
				config.bLog = FALSE;
				break;

			case 'P':
				config.nPageSize = atoi(optarg);//B为单位
				break;

			case 'f':
				config.fFactor = atof(optarg);
				break;
			
			default:
				fprintf(stderr, "选项%c是无效的选项\n", nOpt);
				exit(EXIT_SUCCESS);
		}
	}
}

static void CoreResource()
{
	struct rlimit limit;
	
	//进程自动处理资源限制
	if (config.bSysCore)
	{
		struct rlimit limit_new;
		if (getrlimit(RLIMIT_CORE, &limit) == 0)
		{
			limit_new.rlim_cur = limit.rlim_max;
			limit_new.rlim_max = limit.rlim_max;
			setrlimit(RLIMIT_CORE, &limit_new);
			
			if (getrlimit(RLIMIT_CORE, &limit) != 0 || limit.rlim_cur <= 0)
			{
				fprintf("设置core文件大小限制失败\n");
				exit(EXIT_SUCCESS);
			}
		}
	}
	
	//设置最大连接数
	if (getrlimit(RLIMIT_NOFILE, &limit) == 0)
	{
		int nMaxConn = config.nMaxConn;
		
		if (nMaxConn > limit.rlim_cur)
		{
			limit.rlim_cur = nMaxConn + 3;
		}
		
		if (limit.rlim_cur > limit.rlim_max)
		{
			limit.rlim_max = limit.rlim_cur;/*超级用户 系统管理员*/
		}
		
		if (setrlimit(RLIMIT_NOFILE, &limit) != 0)
		{
			fprintf(stderr, "设置进程的最大连接数失败\n");
			exit(EXIT_FAILED);
		}
	}
}

static void Daemonize()
{
	pid_t pid; 
	pid = fork();
	
	if (pid < 0)
	{
		fprintf(stderr, "创建Daemonize进程失败\n");
		exit(EXIT_FAILED);
	}
	
	if (pid > 0)
	{
		exit(EXIT_SUCCESS);
	}
	
	if (setsid() == -1)
	{
		fprintf(stderr, "守护进程会话设置失败");
		exit(EXIT_FAILED);
	}
	
    chdir("/");
	
	//将文件重定位
	if (config.bLog)
	{
		nFd = open("./easycachedLog.txt", O_WRONLY | O_CREAT);
	}
	else
	{
		nFd = open("/dev/null", O_WRONLY | O_CREAT);
	}
	
	if (nFd == -1)
	{
		fprintf(stderr, "打开日志文件失败\n");
		return;
	}
	
	//定向输出信息
	if (dup2(nFd, STDOUT_FILENO) == -1 ||
			dup2(nFd, STDIN_FILENO) == -1 ||
			dup2(nFd, STDERR_FILENO) == -1)
	{
		fprintf(stderr, "定向输出日志文件失败\n");
	}
	
	if (nFd > STDERR_FILENO)
	{
		close(nFd);
	}
}

static void *EventProcess(void *pArg)
{
	Thread *pThread = (Thread *)pArg;

	//初始化参数
	pThread->bExit = FALSE;
	pThread->pConnQueue = NULL;
	pThread->pTail = NULL;
	pThread->threadId = pthread_self();

	pthread_mutexattr_t mutexAttr;
	pthread_mutexattr_init(&pThread->queueMutex); 
	if (pthread_mutex_init(&pThread->queueMutex, &mutexAttr) != 0)
	{
		fprintf(stderr, "%s\n", "初始化线程队列互斥锁失败");
		pthread_exit("线程失败");
	}

	pthread_mutex_lock(&init_thread_mutex);
	nThreadCount++;
	pthread_mutex_unlock(&init_thread_mutex);

	while (!pThread->bExit)
	{
		//循环处理消息
	}
	return NULL;
}

static void InitThread(int nThreadNumber)
{
	if (nThreadNumber <= 0)
	{
		fprintf(stderr, "%s\n", "创建worker线程的数量不能小于0");
		exit(EXIT_FAILED);
	}

	pThreadArray = malloc(sizeof(Thread) * nThreadNumber);
	if (!pThreadArray)
	{
		fprintf(stderr, "%s\n", "线程创建失败");
		exit(EXIT_FAILED);	
	}

	pthread_mutexattr_t mutexAttr;
	pthread_mutexattr_init(&mutexAttr); 
	if (pthread_mutex_init(&init_thread_mutex, &mutexAttr) != 0)
	{
		fprintf(stderr, "%s\n", "初始化线程互斥锁失败");
		exit(EXIT_FAILED);
	}

	pthread_attr_t threadAttr;
	pthread_t thId;
	（void）pthread_attr_init(&threadAttr);
	for (int i = 0; i < nThreadNumber; i++)
	{
		if (thread_create(&thId, &threadAttr, EventProcess, (void *)pThreadArray[i]) != 0)
		{
			fprintf(stderr, "创建线程%s失败\n", i + 1);
		}
	}

	while (!(nThreadCount == nThreadNumber));
}

static void PushConnection(Connection *pConn, int nThreadNumber)
{
	if (!pConn)
	{
		fprintf(stderr, "%s\n", "不能将空连接加入");
		return;
	}

	Thread *pThread = pThreadArray[nLoadBalanceIndex++];
	if (pThread)
	{
		pthread_mutex_lock(&pThread->queueMutex);
		((!pThread->pConnQueue) ? pThread->pConnQueue : pThread->pTail->pNext) = pConn;
		pThread->pTail = pConn;
		pthread_mutex_unlock(&pThread->queueMutex);

		//加入事件
	}

	//将负载均衡索引下移
	nLoadBalanceIndex = nLoadBalanceIndex % nThreadNumber;
}

int main(int argc, char **argv)
{
	//将所有的错误信息不用缓存即可打印出来
	setbuf(stderr, NULL);
	
	//初始化设置
	InitConfig(argc, argv);
	
	//设置系统资源
	CoreResource();
	
	//开启守护进程
	if (config.bDaemonize)
	{
		Daemonize();
	}
	
	//开启线程
	InitThread(config.nThreadNumber);
	
	return 0;
}