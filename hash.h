#include "slab.h"

#define HashSize(nOffset) (1 << (nOffset))
#define HashMask(n) (HashSize(n) - 1)

struct HashTable
{
	int nOffset;
	Item *pHashTable;
};

typedef struct HashTable HashTable;
static HashTable hashTable;

int InitHash(int nOffset);
uint32_t HashKey( const void *key, size_t length, const uint32_t initval);
int PushHash(Item *pItem);
Item *GetHash(const char *pKey, int nSize);
int DeleteHash(const char *pKey, int nSize);
int ExpandHash(int nOffset);