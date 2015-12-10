//easycached 缓存服务器启动
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/resource.h>
#include <fcntl.h>

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
#define TRUE 1
#define FALSE 0

struct Config
{
	double dMemCachedSize; /*MB为单位*/
	char *sSockIp;//套接字IP 
	int nPort;//监听端口
	int nSockType;//1-tcp 2-udp
	int nThreadNumber;//处理任务的线程数
	BOOL bDaemonize;//守护进程
	int nChunkSize;//每一个块内存大小
	BOOL bSysCore;//修改进程的资源限制
	BOOL bLog;//打开运行日志
};

typedef struct Config Config;
static Config config;

static void DefaultConfig()
{
	//默认初始化
	config.dMemCachedSize = 64.0;
	config.sSockIp = INDRR_ANY;
	config.nPort = 6688;
	config.nSockType = 1;
	config.nThreadNumber = 4;
	config.bDaemonize = FALSE;
	config.nChunkSize = 40;
	config.bSysCore = FALSE;
	config.nMaxConn = 1024;
	config.bLog = TRUE;
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
			)) != -1)
	{
		switch (nOpt)
		{
			case 'm':
				config.dMemCachedSize = atof(optarg);
				if (config.dMemCachedSize <= 0)
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

int main(int argc, char **argv)
{
	//将所有的错误信息不用缓存即可打印出来
	setbuf(stderr, NULL);
	
	//初始化设置
	InitConfig(argc, argv);
	
	//设置系统资源
	CoreResource();
	
	//开启守护进程
	if (bDaemonize)
	{
		Daemonize();
	}
	
	//开启线程
	InitThread();
	
	return 0;
}