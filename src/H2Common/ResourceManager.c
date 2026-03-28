//
// ResourceManager.c
//
// Copyright 1998 Raven Software
//

#ifdef _DEBUG
#include <windows.h>
#endif

#include <stdlib.h>
#include <assert.h>
#include <stdint.h>

#include "ResourceManager.h"
#include "q_shared.h"

typedef struct ResMngr_Block_s
{
	unsigned char* start;
	size_t size;
	struct ResMngr_Block_s* next;
} ResMngr_Block_t;

static size_t ResMngr_AlignUp(size_t value, size_t alignment)
{
	return (value + (alignment - 1)) & ~(alignment - 1);
}

static void ResMngr_CreateBlock(ResourceManager_t* resource)
{
	const size_t alignment = sizeof(void*);
	const size_t block_size = resource->nodeSize * resource->resPerBlock;
	unsigned char* block = (unsigned char*)malloc(block_size);

	assert(block);

	ResMngr_Block_t* temp = (ResMngr_Block_t*)malloc(sizeof(ResMngr_Block_t));
	assert(temp);

	temp->start = block;
	temp->size = block_size;
	temp->next = (ResMngr_Block_t*)resource->blockList;
	resource->blockList = temp;

	resource->free = (void**)block;

	void** current = resource->free;

	for (size_t i = 0; i < resource->resPerBlock - 1; ++i)
	{
		void* next = (void*)((unsigned char*)current + resource->nodeSize);
		*current = next;
		current = (void**)next;
	}

	*current = NULL;
}

H2COMMON_API void ResMngr_Con(ResourceManager_t* resource, const size_t init_resSize, const size_t init_resPerBlock)
{
	const size_t alignment = sizeof(void*);

	resource->resSize = init_resSize;
	resource->resPerBlock = init_resPerBlock;

	// Each node is:
	// [ next pointer ][ user payload ]
	// and the whole node must remain pointer-aligned on x86/x64.
	resource->nodeSize = ResMngr_AlignUp(sizeof(void*) + resource->resSize, alignment);

	resource->blockList = NULL;
	resource->free = NULL;

#ifdef _DEBUG
	resource->numResourcesAllocated = 0;
#endif

	ResMngr_CreateBlock(resource);
}

// ResourceManager destructor.
H2COMMON_API void ResMngr_Des(ResourceManager_t* resource)
{
#ifdef _DEBUG
	if (resource->numResourcesAllocated > 0)
	{
		char msg[128];
		Com_sprintf(msg, sizeof(msg),
			"Potential memory leak: %zu bytes unfreed\n",
			resource->resSize * resource->numResourcesAllocated);
		OutputDebugStringA(msg);
	}
#endif

	while (resource->blockList)
	{
		ResMngr_Block_t* toDelete = (ResMngr_Block_t*)resource->blockList;
		resource->blockList = toDelete->next;
		free(toDelete->start);
		free(toDelete);
	}

	resource->free = NULL;
}

H2COMMON_API void* ResMngr_AllocateResource(ResourceManager_t* resource, const size_t size)
{
	assert(size == resource->resSize);

#ifdef _DEBUG
	resource->numResourcesAllocated++;
#endif

	assert(resource->free);

	void** toPop = resource->free;

	// Advance free list.
	resource->free = (void**)(*toPop);
	if (resource->free == NULL)
	{
		ResMngr_CreateBlock(resource);
	}

	// Clear next pointer in allocated node.
	*toPop = NULL;

	// Payload begins immediately after the next pointer.
	return (void*)(toPop + 1);
}

H2COMMON_API void ResMngr_DeallocateResource(ResourceManager_t* resource, void* toDeallocate, const size_t size)
{
	assert(size == resource->resSize);

#ifdef _DEBUG
	assert(resource->numResourcesAllocated > 0);
	resource->numResourcesAllocated--;
#endif

	assert(resource->free);
	assert(toDeallocate);

	void** toPush = ((void**)toDeallocate) - 1;

	*toPush = (void*)resource->free;
	resource->free = toPush;
}