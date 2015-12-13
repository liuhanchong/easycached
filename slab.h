//字节对齐
#define ALIGN_SIZE 4

struct Item
{
	void *pKey;
	int nKeySize;
	void *pValue;
	int nValueSize;
	int nFree;
	Item *pNext;
};

typedef struct Item Item;

struct Slag
{
	Item *pItemQueue;
	Item *pItemTail;
	int nChunkSize;
	int nChunkNumber;
	int nFreeChunkNumber;
	Slag *pNext;
};

typedef struct Slag Slag;

struct Class
{
	int nMemSize;
	int nPageSize;
	int nBaseChunkSize;
	float fFactor;
	void *pMemory;
	Slag *pSlagQueue;
	Slag *pSlagTail;
	int nSlagNumber;
	int nCurSlagNumber;
	int *pSlagClass;
	int nClassSize;
};

typedef struct Class Class;

static Class class;

//计算对齐的字节数
#define ALIGN_MEM(SIZE) ((SIZE + sizeof(Item)) + (ALIGN_SIZE - (SIZE + sizeof(Item)) % ALIGN_SIZE))

void InitSlab(int nMemSize, int nPageSize, int nBaseChunkSize, float fFactor);

int PushItem(Item *pItem);