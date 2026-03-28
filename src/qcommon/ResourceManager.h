//
// ResourceManager.h
//
// Copyright 1998 Raven Software
//

#pragma once

#include "H2Common.h"
#include "q_Typedef.h" //mxd. For uint...

typedef struct ResourceManager_s
{
	size_t resSize;
	size_t resPerBlock;
	size_t nodeSize;
	void** free;
	struct ResMngr_Block_s* blockList;
#ifdef _DEBUG
	size_t numResourcesAllocated;
#endif
} ResourceManager_t;

extern H2COMMON_API void ResMngr_Con(ResourceManager_t* resource, uint init_resSize, uint init_resPerBlock);
extern H2COMMON_API void ResMngr_Des(ResourceManager_t* resource);
extern H2COMMON_API void* ResMngr_AllocateResource(ResourceManager_t* resource, uint size);
extern H2COMMON_API void ResMngr_DeallocateResource(ResourceManager_t* resource, void* toDeallocate, uint size);
