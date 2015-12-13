#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include "slab.h"

void InitSlab(int nMemSize, int nPageSize, int nBaseChunkSize, float fFactor)
{
	//初始化
	class.fFactor = fFactor;
	class.nBaseChunkSize = nBaseChunkSize;
	class.nMemSize = nMemSize;
	class.nPageSize = nPageSize;
	class.pMemory = NULL;
	class.pSlagClass = NULL;
	class.pSlagQueue = NULL;
	class.pSlagTail = NULL;
	class.nClassSize = 0;
	class.nSlagNumber = nMemSize / nPageSize;
	class.nCurSlagNumber = 0;

	class.pMemory = malloc(nMemSize * 1024 * 1024);
	if (!class.pMemory)
	{
		fprintf(stderr, "%s\n", "申请内存池失败");
		exit(1);
	}

	//计算种类数
	int nMark = nPageSize * 1024 * 1024 / nBaseChunkSize;
	int nCount = 0;
	for (double dClass = 1; dClass < nMark; dClass *= fFactor, class.nClassSize++);

	//初始化种类数组
	class.pSlagClass = (int *)malloc(sizeof(int) * class.nClassSize);
	if (!class.pSlagClass)
	{
		fprintf(stderr, "%s\n", "申请内存种类数组失败");
		exit(1);
	}

	//计算可能分配大小的所有种类
	for (int i = 0; i < class.nClassSize; i++)
	{
		class.pSlagClass[i] = ALIGN_MEM(nBaseChunkSize * pow(fFactor, i));
	}
}

int GetClass(int nSize)
{
	for (int i = 0; i < class.nClassSize; i++)
	{
		if (nSize < class.pSlagClass[i])
		{
			return class.pSlagClass[i];
		}
	}

	return 0;
}

int PushItem(Item *pItem)
{
	int nSize = pItem->nKeySize + pItem->nValueSize + sizeof(Item);
	int nAlignSize = GetClass(nSize);
	if (nAlignSize <= 0)
	{
		fprintf(stderr, "未找到符合%d的对齐尺寸\n", nSize);
		return 0;
	}

	Slag *pHead = class.pSlagQueue;
	while (!pHead)
	{
		if (pHead->nChunkSize == nAlignSize)
		{
			break;
		}

		pHead = pHead->pNext;
	}

	//找到了
	if (!pHead)
	{
		if (pHead->nFreeChunkNumber > 0)
		{
			Item *pItemHead = pHead->pItemQueue;
			while (pItemHead)
			{
				if (pItemHead->nFree == 1)
				{
					memcpy(pHead->pItemTail, pItem, sizeof(Item));
					pNewSlg->pItemTail += sizeof(Item);
					memcpy(pNewSlg->pItemTail, pItem->pKey, pItem->nKeySize);
					pNewSlg->pItemTail += pItem->nKeySize;
					memcpy(pNewSlg->pItemTail, pItem->pValue, pItem->nValueSize);
					pNewSlg->pItemTail += pItem->nValueSize;

					//给当前slag减少free数量
					pNewSlg->nFreeChunkNumber--;
					return 1;
				}

				pItemHead = pItemHead->pNext;
			}
		}
	}

	if (class.nCurSlagNumber >= class.nSlagNumber)
	{
		fprintf(stderr, "申请的内存已经全部用完，无法再次分配slag\n");
		return 0;
	}

	//加入一个对齐尺寸的大小
	Slag *pNewSlg = (Slag *)malloc(sizeof(Slag));
	if (!pNewSlg)
	{
		fprintf(stderr, "分配slag失败\n");
		return 0;
	}

	//新建一个slag
	pNewSlg->nChunkSize = nAlignSize;
	pNewSlg->nChunkNumber = class.nPageSize / nAlignSize;
	pNewSlg->nFreeChunkNumber = pNewSlg->nChunkNumber;
	pNewSlg->pItemQueue = pMemory + class.nCurSlagNumber;
	pNewSlg->pItemTail = pNewSlg->pItemQueue;
	pNewSlg->pNext = NULL;

	//将其加入到class队列
	((!class.pSlagQueue) ? class.pSlagQueue : class.pSlagTail->pNext) = pNewSlg;
	class.pSlagTail = pNewSlg;
	class.nCurSlagNumber++;

	//将元素加入内存
	memcpy(pNewSlg->pItemTail, pItem, sizeof(Item));
	pNewSlg->pItemTail += sizeof(Item);
	memcpy(pNewSlg->pItemTail, pItem->pKey, pItem->nKeySize);
	pNewSlg->pItemTail += pItem->nKeySize;
	memcpy(pNewSlg->pItemTail, pItem->pValue, pItem->nValueSize);
	pNewSlg->pItemTail += pItem->nValueSize;

	//给当前slag减少free数量
	pNewSlg->nFreeChunkNumber--;

	return 1;
}