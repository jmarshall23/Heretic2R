//
// gl1_FlexModel.c -- Heretic 2 FlexModel loading.
//
// Copyright 1998 Raven Software
//

#include "gl_FlexModel.h"
#include "gl_Image.h"
#include "gl_Light.h"
#include "gl_Misc.h"
#include "Skeletons/r_Skeletons.h"
#include "Skeletons/r_SkeletonLerp.h"
#include "anorms.h"
#include "anormtab.h"
#include "Hunk.h"
#include "Vector.h"

#define MAX_DXR_ENTITY_CACHES 5000

static void R_InterpolateVertexNormals(const int num_xyz, const float lerp, const fmtrivertx_t* verts, const fmtrivertx_t* old_verts, vec3_t* normals);

dxrEntityCache_t g_dxrEntityCaches[MAX_DXR_ENTITY_CACHES];
dxrEntityCache_t g_dxrAlphaEntityCaches[MAX_DXR_ENTITY_CACHES];
bool g_dxrAnyTLASChange = false;

static dxrEntityCache_t* R_GetDXRCacheForEntity(const entity_t* e)
{
	for (int i = 0; i < r_newrefdef.num_entities && i < MAX_DXR_ENTITY_CACHES; ++i)
	{
		if (r_newrefdef.entities[i] == e)
			return &g_dxrEntityCaches[i];
	}

	for (int i = 0; i < r_newrefdef.num_alpha_entities && i < MAX_DXR_ENTITY_CACHES; ++i)
	{
		if (r_newrefdef.alpha_entities[i] == e)
			return &g_dxrAlphaEntityCaches[i];
	}

	return NULL;
}


static uint32_t Mod_CountFlexModelIndices(const fmdl_t* fmdl)
{
	uint32_t total = 0;

	for (int i = 0; i < fmdl->header.num_mesh_nodes; ++i)
	{
		int* order = &fmdl->glcmds[fmdl->mesh_nodes[i].start_glcmds];

		for (;;)
		{
			int numVerts = *order++;
			if (numVerts == 0)
				break;

			const qboolean isFan = (numVerts < 0);
			if (isFan)
				numVerts = -numVerts;

			if (numVerts >= 3)
				total += (uint32_t)((numVerts - 2) * 3);

			order += numVerts * 3;
		}
	}

	return total;
}

static void Mod_BuildFlexModelIndexBuffer(model_t* mod, const fmdl_t* fmdl)
{
	mod->dxrVertexCount = (uint32_t)fmdl->header.num_xyz;
	mod->dxrIndexCount = Mod_CountFlexModelIndices(fmdl);

	if(mod->dxrIndices == NULL)
		mod->dxrIndices = (uint32_t*)Hunk_Alloc((int)(sizeof(uint32_t) * mod->dxrIndexCount));

	uint32_t* out = mod->dxrIndices;

	for (int i = 0; i < fmdl->header.num_mesh_nodes; ++i)
	{
		int* order = &fmdl->glcmds[fmdl->mesh_nodes[i].start_glcmds];

		for (;;)
		{
			int numVerts = *order++;
			if (numVerts == 0)
				break;

			qboolean isFan = false;
			if (numVerts < 0)
			{
				numVerts = -numVerts;
				isFan = true;
			}

			if (numVerts < 3)
			{
				order += numVerts * 3;
				continue;
			}

			uint32_t local[1024]; // or temp alloc if you want safer sizing
			assert(numVerts < 1024);

			for (int v = 0; v < numVerts; ++v)
			{
				local[v] = (uint32_t)order[2];
				order += 3;
			}

			if (isFan)
			{
				for (int v = 1; v + 1 < numVerts; ++v)
				{
					*out++ = local[0];
					*out++ = local[v];
					*out++ = local[v + 1];
				}
			}
			else
			{
				for (int v = 0; v + 2 < numVerts; ++v)
				{
					if ((v & 1) == 0)
					{
						*out++ = local[v + 0];
						*out++ = local[v + 1];
						*out++ = local[v + 2];
					}
					else
					{
						*out++ = local[v + 1];
						*out++ = local[v + 0];
						*out++ = local[v + 2];
					}
				}
			}
		}
	}
}

static void R_InterpolateFlexNormalsForDXR(const fmdl_t* fmdl, entity_t* e, vec3_t* outNormals)
{
	if (fmdl->frames == NULL)
	{
		for (int i = 0; i < fmdl->header.num_xyz; ++i)
		{
			const int n = fmdl->lightnormalindex[i];
			VectorCopy(bytedirs[n], outNormals[i]);
		}
		return;
	}

	R_InterpolateVertexNormals(
		fmdl->header.num_xyz,
		e->backlerp,
		sfl_cur_skel.verts,
		sfl_cur_skel.old_verts,
		outNormals);
}

static void R_FillFlexDXRVertices(model_t* mod, const fmdl_t* fmdl, entity_t* e, dxrEntityCache_t* cache)
{
	static vec3_t normals[MAX_FM_VERTS];

	FrameLerp(fmdl, e);
	R_InterpolateFlexNormalsForDXR(fmdl, e, normals);

	if (cache->vertices == NULL)
	{
		cache->vertexCount = (uint32_t)fmdl->header.num_xyz;
		cache->indexCount = mod->dxrIndexCount;

		cache->vertices = (glRaytracingVertex_t*)malloc(sizeof(glRaytracingVertex_t) * cache->vertexCount);
		cache->indices = (uint32_t*)malloc(sizeof(uint32_t) * cache->indexCount);

		memcpy(cache->indices, mod->dxrIndices, sizeof(uint32_t) * cache->indexCount);
	}

	for (uint32_t i = 0; i < cache->vertexCount; ++i)
	{
		cache->vertices[i].xyz[0] = s_lerped[i][0];
		cache->vertices[i].xyz[1] = s_lerped[i][1];
		cache->vertices[i].xyz[2] = s_lerped[i][2];

		cache->vertices[i].normal[0] = normals[i][0];
		cache->vertices[i].normal[1] = normals[i][1];
		cache->vertices[i].normal[2] = normals[i][2];

		cache->vertices[i].st[0] = 0.0f;
		cache->vertices[i].st[1] = 0.0f;
	}
}

static void R_UpdateFlexModelDXRMesh(model_t* mod, const fmdl_t* fmdl, entity_t* e, dxrEntityCache_t* cache)
{
	Mod_BuildFlexModelIndexBuffer(mod, fmdl);
	R_FillFlexDXRVertices(mod, fmdl, e, cache);

	glRaytracingMeshDesc_t meshDesc;
	memset(&meshDesc, 0, sizeof(meshDesc));
	meshDesc.vertices = cache->vertices;
	meshDesc.vertexCount = cache->vertexCount;
	meshDesc.indices = cache->indices;
	meshDesc.indexCount = cache->indexCount;
	meshDesc.allowUpdate = 1;
	meshDesc.opaque = 1;

	if (!cache->meshValid)
	{
		cache->meshHandle = glRaytracingCreateMesh(&meshDesc);
		cache->meshValid = (cache->meshHandle != 0);
	}
	else
	{
		glRaytracingUpdateMesh(cache->meshHandle, &meshDesc);
	}
}

static void R_BuildEntityTransform3x4(const entity_t* e, float out[12])
{
	vec3_t angles =
	{
		e->angles[0] * RAD_TO_ANGLE,
		e->angles[1] * RAD_TO_ANGLE * -1.0f,
		e->angles[2] * RAD_TO_ANGLE,
	};

	vec3_t forward, right, up;
	AngleVectors(angles, forward, right, up);

	const float s = (e->cl_scale != 0.0f ? e->cl_scale : 1.0f);

	out[0] = forward[0] * s;
	out[1] = -right[0] * s;
	out[2] = up[0] * s;
	out[3] = e->origin[0];

	out[4] = forward[1] * s;
	out[5] = -right[1] * s;
	out[6] = up[1] * s;
	out[7] = e->origin[1];

	out[8] = forward[2] * s;
	out[9] = -right[2] * s;
	out[10] = up[2] * s;
	out[11] = e->origin[2];
}

static qboolean R_FlexModelPoseChanged(const entity_t* e, const dxrEntityCache_t* cache)
{
	if (e->frame != cache->lastFrame) return true;
	if (e->oldframe != cache->lastOldFrame) return true;
	if (fabsf(e->backlerp - cache->lastBacklerp) > 0.0001f) return true;
	if (e->cl_scale != cache->lastScale) return true;
	return false;
}

static qboolean R_FlexModelTransformChanged(const entity_t* e, const dxrEntityCache_t* cache)
{
	if (memcmp(e->origin, cache->lastOrigin, sizeof(vec3_t)) != 0) return true;
	if (memcmp(e->angles, cache->lastAngles, sizeof(vec3_t)) != 0) return true;
	return false;
}

static void R_StoreFlexModelState(const entity_t* e, dxrEntityCache_t* cache)
{
	cache->lastFrame = e->frame;
	cache->lastOldFrame = e->oldframe;
	cache->lastBacklerp = e->backlerp;
	cache->lastScale = e->cl_scale;
	VectorCopy(e->origin, cache->lastOrigin);
	VectorCopy(e->angles, cache->lastAngles);
}

void R_EnsureFlexModelInDXR(entity_t* e)
{
	model_t* mod = *e->model;
	fmdl_t* fmdl = (fmdl_t*)mod->extradata;
	dxrEntityCache_t* cache = R_GetDXRCacheForEntity(e);

	cache->touchedThisFrame = true;

	const qboolean poseChanged = (!cache->meshValid || R_FlexModelPoseChanged(e, cache));
	const qboolean xformChanged = (!cache->meshValid || R_FlexModelTransformChanged(e, cache));

	if (poseChanged)
	{
		R_UpdateFlexModelDXRMesh(mod, fmdl, e, cache);
		g_dxrAnyTLASChange = true;
	}

	if (cache->meshValid && (xformChanged || cache->instanceHandle == 0))
	{
		glRaytracingInstanceDesc_t instDesc;
		memset(&instDesc, 0, sizeof(instDesc));
		instDesc.meshHandle = cache->meshHandle;
		instDesc.instanceID = (uint32_t)(cache - g_dxrEntityCaches);
		instDesc.mask = 0xFF;
		R_BuildEntityTransform3x4(e, instDesc.transform);

		if (cache->instanceHandle == 0)
			cache->instanceHandle = glRaytracingCreateInstance(&instDesc);
		else
			glRaytracingUpdateInstance(cache->instanceHandle, &instDesc);

		g_dxrAnyTLASChange = true;
	}

	R_StoreFlexModelState(e, cache);
}

#pragma region ========================== FLEX MODEL LOADING ==========================

static qboolean fmLoadHeader(fmdl_t* fmdl, model_t* model, const int version, const int datasize, const void* buffer)
{
	if (version != FM_HEADER_VER)
		ri.Sys_Error(ERR_DROP, "Invalid HEADER version for block %s: %d != %d\n", FM_HEADER_NAME, FM_HEADER_VER, version);

	// Read header...
	memcpy(&fmdl->header, buffer, sizeof(fmheader_t));

	// Sanity checks...
	const fmheader_t* h = &fmdl->header;

	if (h->skinwidth < 1 || h->skinwidth > SKINPAGE_WIDTH || h->skinheight < 1 || h->skinheight > SKINPAGE_HEIGHT) //mxd. Added SKINPAGE_WIDTH check.
		ri.Sys_Error(ERR_DROP, "Model '%s' has invalid skin size (%ix%i)", model->name, h->skinwidth, h->skinheight);

	if (h->num_xyz < 1 || h->num_xyz > MAX_FM_VERTS)
		ri.Sys_Error(ERR_DROP, "Model '%s' has invalid number of vertices (%i)", model->name, h->num_xyz);

	if (h->num_st < 1)
		ri.Sys_Error(ERR_DROP, "Model '%s' has no st vertices", model->name);

	if (h->num_tris < 1 || h->num_tris > MAX_FM_TRIANGLES) //mxd. Added MAX_FM_TRIANGLES check.
		ri.Sys_Error(ERR_DROP, "Model '%s' has invalid number of triangles (%i)", model->name, h->num_tris);

	if (h->num_frames < 1 || h->num_frames > MAX_FM_FRAMES) //mxd. Added MAX_FM_FRAMES check.
		ri.Sys_Error(ERR_DROP, "Model '%s' has invalid number of frames (%i)", model->name, h->num_frames);

	VectorSet(model->mins, -32.0f, -32.0f, -32.0f);
	VectorSet(model->maxs, 32.0f, 32.0f, 32.0f);

	return true;
}

static qboolean fmLoadSkin(fmdl_t* fmdl, model_t* model, const int version, const int datasize, const void* buffer)
{
	if (version != FM_SKIN_VER)
		ri.Sys_Error(ERR_DROP, "Invalid SKIN version for block %s: %d != %d\n", FM_SKIN_NAME, FM_SKIN_VER, version);

	const int skin_names_size = fmdl->header.num_skins * MAX_FRAMENAME;
	if (skin_names_size != datasize)
	{
		ri.Con_Printf(PRINT_ALL, "Skin sizes do not match: %d != %d\n", datasize, skin_names_size);
		return false;
	}

	fmdl->skin_names = (char*)Hunk_Alloc(skin_names_size);
	memcpy(fmdl->skin_names, buffer, skin_names_size);

	// Precache skins...
	char* skin_name = fmdl->skin_names;
	for (int i = 0; i < fmdl->header.num_skins; i++, skin_name += MAX_FRAMENAME)
		model->skins[i] = R_FindImage(skin_name, it_skin);

	return true;
}

static qboolean fmLoadST(fmdl_t* fmdl, model_t* model, const int version, const int datasize, const void* buffer)
{
	return true;
}

static qboolean fmLoadTris(fmdl_t* fmdl, model_t* model, const int version, const int datasize, const void* buffer)
{
	return true;
}

static qboolean fmLoadFrames(fmdl_t* fmdl, model_t* model, const int version, const int datasize, const void* buffer)
{
	if (version != FM_FRAME_VER)
		ri.Sys_Error(ERR_DROP, "Invalid FRAMES version for block %s: %d != %d\n", FM_FRAME_NAME, FM_FRAME_VER, version);

	fmdl->frames = (fmaliasframe_t*) Hunk_Alloc(fmdl->header.num_frames * fmdl->header.framesize);

	for (int i = 0; i < fmdl->header.num_frames; i++)
	{
		const fmaliasframe_t* in = (const fmaliasframe_t*)((const byte*)buffer + i * fmdl->header.framesize);
		fmaliasframe_t* out = (fmaliasframe_t*)((byte*)fmdl->frames + i * fmdl->header.framesize);

		VectorCopy(in->scale, out->scale);
		VectorCopy(in->translate, out->translate);

		memcpy(out->name, in->name, sizeof(out->name));
		memcpy(out->verts, in->verts, fmdl->header.num_xyz * sizeof(fmtrivertx_t));
	}

	return true;
}

static qboolean fmLoadGLCmds(fmdl_t* fmdl, model_t* model, const int version, const int datasize, const void* buffer)
{
	if (version != FM_GLCMDS_VER)
		ri.Sys_Error(ERR_DROP, "Invalid GLCMDS version for block %s: %d != %d\n", FM_GLCMDS_NAME, FM_GLCMDS_VER, version);

	const uint size = fmdl->header.num_glcmds * sizeof(int);
	fmdl->glcmds = (int *)Hunk_Alloc((int)size);
	memcpy(fmdl->glcmds, buffer, size);

	return true;
}

static qboolean fmLoadMeshNodes(fmdl_t* fmdl, model_t* model, const int version, const int datasize, const void* buffer)
{
	if (version != FM_MESH_VER)
		ri.Sys_Error(ERR_DROP, "Invalid MESH version for block %s: %d != %d\n", FM_MESH_NAME, FM_MESH_VER, version);

	if (fmdl->header.num_mesh_nodes < 1)
		return true;

	fmdl->mesh_nodes = (fmmeshnode_t *)Hunk_Alloc(fmdl->header.num_mesh_nodes * (int)sizeof(fmmeshnode_t));

	const fmmeshnode_t* in = (const fmmeshnode_t * )buffer;
	fmmeshnode_t* out = &fmdl->mesh_nodes[0];

	for (int i = 0; i < fmdl->header.num_mesh_nodes; i++, in++, out++)
	{
		//mxd. Don't copy tris (unused).

		// Copy verts.
		memcpy(out->verts, in->verts, sizeof(out->verts));

		// Copy glcmds.
		out->start_glcmds = in->start_glcmds;
		out->num_glcmds = in->num_glcmds;
	}

	return true;
}

//mxd. FM_SHORT_FRAME, FM_NORMAL and FM_COMP blocks are never used in any of H2 models.
static qboolean fmSkipBlock(fmdl_t* fmdl, model_t* model, const int version, const int datasize, const void* buffer)
{
	return false;
}

static qboolean fmLoadSkeleton(fmdl_t* fmdl, model_t* model, const int version, const int datasize, const void* buffer)
{
	if (version != FM_SKELETON_VER)
	{
		ri.Con_Printf(PRINT_ALL, "Invalid SKELETON version for block %s: %d != %d\n", FM_SKELETON_NAME, FM_SKELETON_VER, version);
		return false;
	}

	const int* in_i = (const int* )buffer;

	fmdl->skeletalType = *in_i;
	fmdl->rootCluster = CreateSkeleton(fmdl->skeletalType);

	const int num_clusters = *(++in_i);

	// Count and allocate verts...
	int num_verts = 0;
	for (int cluster = num_clusters - 1; cluster > -1; cluster--)
	{
		num_verts += *(++in_i);

		const int cluster_index = fmdl->rootCluster + cluster;
		SkeletalClusters[cluster_index].numVerticies = num_verts;
		SkeletalClusters[cluster_index].verticies = (int *)Hunk_Alloc(num_verts * (int)sizeof(int));
	}

	int start_vert_index = 0;
	for (int cluster = num_clusters - 1; cluster > -1; cluster--)
	{
		for (int v = start_vert_index; v < SkeletalClusters[fmdl->rootCluster + cluster].numVerticies; v++)
		{
			const int vert_index = *(++in_i);
			for (int c = 0; c <= cluster; c++)
				SkeletalClusters[fmdl->rootCluster + c].verticies[v] = vert_index;
		}

		start_vert_index = SkeletalClusters[fmdl->rootCluster + cluster].numVerticies;
	}

	// Check for duplicates...
	for (int i = 0; i < num_clusters; i++)
	{
		const int c = fmdl->rootCluster + i;
		for (int v1 = 0; v1 < SkeletalClusters[c].numVerticies - 1; v1++)
			for (int v2 = v1 + 1; v2 < SkeletalClusters[c].numVerticies; v2++)
				if (SkeletalClusters[c].verticies[v1] == SkeletalClusters[c].verticies[v2])
					ri.Con_Printf(PRINT_ALL, "Warning: duplicate skeletal cluster vertex: %d\n", SkeletalClusters[c].verticies[v1]); //mxd. Com_Printf() -> ri.Con_Printf().
	}

	const qboolean have_skeleton = *(++in_i);

	// Create skeleton.
	if (have_skeleton)
	{
		const float* in_f = (const float*)in_i;

		fmdl->skeletons = (ModelSkeleton_t *)Hunk_Alloc(fmdl->header.num_frames * (int)sizeof(ModelSkeleton_t));

		for (int i = 0; i < fmdl->header.num_frames; i++)
		{
			CreateSkeletonAsHunk(fmdl->skeletalType, fmdl->skeletons + i);

			for (int c = 0; c < num_clusters; c++)
			{
				fmdl->skeletons[i].rootJoint[c].model.origin[0] = *(++in_f);
				fmdl->skeletons[i].rootJoint[c].model.origin[1] = *(++in_f);
				fmdl->skeletons[i].rootJoint[c].model.origin[2] = *(++in_f);

				fmdl->skeletons[i].rootJoint[c].model.direction[0] = *(++in_f);
				fmdl->skeletons[i].rootJoint[c].model.direction[1] = *(++in_f);
				fmdl->skeletons[i].rootJoint[c].model.direction[2] = *(++in_f);

				fmdl->skeletons[i].rootJoint[c].model.up[0] = *(++in_f);
				fmdl->skeletons[i].rootJoint[c].model.up[1] = *(++in_f);
				fmdl->skeletons[i].rootJoint[c].model.up[2] = *(++in_f);

				VectorCopy(fmdl->skeletons[i].rootJoint[c].model.origin,    fmdl->skeletons[i].rootJoint[c].parent.origin);
				VectorCopy(fmdl->skeletons[i].rootJoint[c].model.direction, fmdl->skeletons[i].rootJoint[c].parent.direction);
				VectorCopy(fmdl->skeletons[i].rootJoint[c].model.up,        fmdl->skeletons[i].rootJoint[c].parent.up);
			}
		}
	}
	else
	{
		fmdl->header.num_xyz -= num_clusters * 3;
	}

	return true;
}

static qboolean fmLoadReferences(fmdl_t* fmdl, model_t* model, const int version, const int datasize, const void* buffer)
{
	//mxd. Helper data type...
	typedef struct
	{
		int referenceType;
		qboolean haveRefs;
		Placement_t* refsForFrame;
	} dmreferences_t;

	if (version != FM_REFERENCES_VER)
	{
		ri.Con_Printf(PRINT_ALL, "Invalid REFERENCES version for block %s: %d != %d\n", FM_REFERENCES_NAME, FM_REFERENCES_VER, version);
		return false;
	}

	const dmreferences_t* in = (const dmreferences_t * )buffer;
	fmdl->referenceType = in->referenceType;

	if (!in->haveRefs)
	{
		fmdl->header.num_xyz -= numReferences[fmdl->referenceType] * 3;
		return true;
	}

	const int num_refs = numReferences[fmdl->referenceType];
	fmdl->refsForFrame = (Placement_t*) Hunk_Alloc(fmdl->header.num_frames * num_refs * (int)sizeof(Placement_t));

	if (fmdl->header.num_frames < 1)
		return true;

	const Placement_t* ref_in = (const Placement_t*)&in->refsForFrame;
	Placement_t* ref_out = fmdl->refsForFrame;

	for (int i = 0; i < fmdl->header.num_frames; i++)
	{
		for (int j = 0; j < num_refs; j++, ref_in++, ref_out++)
		{
			for (int c = 0; c < 3; c++)
			{
				//TODO: done this way to perform byte-swapping (otherwise we can just memcpy the whole thing)?
				ref_out->origin[c] = ref_in->origin[c];
				ref_out->direction[c] = ref_in->direction[c];
				ref_out->up[c] = ref_in->up[c];
			}
		}
	}

	return true;
}

// FlexModel block loaders.
typedef struct
{
	char ident[FMDL_BLOCK_IDENT_SIZE];
	qboolean (*load)(fmdl_t* fmdl, model_t* model, int version, int datasize, const void* buffer);
} fmdl_loader_t;

static fmdl_loader_t fmblocks[] =
{
	{ FM_HEADER_NAME,		fmLoadHeader },
	{ FM_SKIN_NAME,			fmLoadSkin },
	{ FM_ST_NAME,			fmLoadST },
	{ FM_TRI_NAME,			fmLoadTris },
	{ FM_FRAME_NAME,		fmLoadFrames },
	{ FM_GLCMDS_NAME,		fmLoadGLCmds },
	{ FM_MESH_NAME,			fmLoadMeshNodes },
	{ FM_SHORT_FRAME_NAME,	fmSkipBlock },
	{ FM_NORMAL_NAME,		fmSkipBlock },
	{ FM_COMP_NAME,			fmSkipBlock },
	{ FM_SKELETON_NAME,		fmLoadSkeleton },
	{ FM_REFERENCES_NAME,	fmLoadReferences },
	{ "",					NULL },
};

void Mod_LoadFlexModel(model_t* mod, void* buffer, int length)
{
	mod->type = mod_fmdl;

	// Stored in mod->extradata.
	fmdl_t* fmdl = (fmdl_t * )Hunk_Alloc(sizeof(fmdl_t));
	fmdl->skeletalType = SKEL_NULL;
	fmdl->referenceType = REF_NULL;

	byte* in = (byte * )buffer;

	while (length > 0)
	{
		fmdl_blockheader_t* header = (fmdl_blockheader_t*)in;
		in += sizeof(fmdl_blockheader_t); // Block data is stored after block header.

		// Find appropriate loader...
		fmdl_loader_t* loader;
		for (loader = &fmblocks[0]; loader->ident[0] != 0; loader++)
		{
			if (Q_stricmp(loader->ident, header->ident) == 0)
			{
				if (!loader->load(fmdl, mod, header->version, header->size, in)) //mxd. Added sanity check.
				{
					ri.Com_Error(ERR_DROP, "Mod_LoadFlexModel: failed to load block %s\n", header->ident); //mxd. Com_Error() -> ri.Com_Error().
					return;
				}

				break;
			}
		}

		if (loader->ident[0] == 0)
			ri.Con_Printf(PRINT_ALL, "Mod_LoadFlexModel: unknown block %s\n", header->ident);

		in += header->size; // Skip to next block header...
		length -= (header->size + (int)sizeof(fmdl_blockheader_t));
	}

	//mxd. Never null?
	assert(fmdl->frames != NULL);
}

void Mod_RegisterFlexModel(model_t* mod)
{
	const fmdl_t* fmdl = (fmdl_t*)mod->extradata;

	// Precache skins... //TODO: also done in fmLoadSkin(). One of these isn't needed?
	char* skin_name = fmdl->skin_names;
	for (int i = 0; i < fmdl->header.num_skins; i++, skin_name += MAX_FRAMENAME)
		mod->skins[i] = R_FindImage(skin_name, it_skin);
}

#pragma endregion

#pragma region ========================== FLEX MODEL RENDERING ==========================

//mxd. Somewhat similar to R_CullAliasModel from Q2.
static qboolean R_CullFlexModel(const fmdl_t* fmdl, entity_t* e)
{
	vec3_t mins;
	vec3_t maxs;

	if (e->frame < 0 || e->frame >= fmdl->header.num_frames)
		e->frame = 0;

	if (e->oldframe < 0 || e->oldframe >= fmdl->header.num_frames)
		e->oldframe = 0;

	// Compute axially aligned mins and maxs.
	if (fmdl->frames == NULL)
	{
		VectorCopy(fmdl->compdata[fmdl->frame_to_group[e->frame]].bmin, mins);
		VectorCopy(fmdl->compdata[fmdl->frame_to_group[e->frame]].bmax, maxs);
	}
	else
	{
		const fmaliasframe_t* pframe = (fmaliasframe_t*)((byte*)fmdl->frames + e->frame * fmdl->header.framesize);
		const fmaliasframe_t* poldframe = (fmaliasframe_t*)((byte*)fmdl->frames + e->oldframe * fmdl->header.framesize);

		if (pframe == poldframe)
		{
			for (int i = 0; i < 3; i++)
			{
				mins[i] = pframe->translate[i];
				maxs[i] = mins[i] + pframe->scale[i] * 255.0f;
			}
		}
		else
		{
			for (int i = 0; i < 3; i++)
			{
				const float thismins = pframe->translate[i];
				const float thismaxs = thismins + pframe->scale[i] * 255.0f;

				const float oldmins = poldframe->translate[i];
				const float oldmaxs = oldmins + poldframe->scale[i] * 255.0f;

				mins[i] = min(thismins, oldmins);
				maxs[i] = max(thismaxs, oldmaxs);
			}
		}
	}

	// Apply model scale.
	if (e->cl_scale != 0.0f && e->cl_scale != 1.0f)
	{
		Vec3ScaleAssign(e->cl_scale, mins);
		Vec3ScaleAssign(e->cl_scale, maxs);
	}

	// Compute a full bounding box.
	vec3_t bbox[8];
	for (int i = 0; i < 8; i++)
	{
		bbox[i][0] = (i & 1 ? mins[0] : maxs[0]);
		bbox[i][1] = (i & 2 ? mins[1] : maxs[1]);
		bbox[i][2] = (i & 4 ? mins[2] : maxs[2]);
	}

	// Rotate the bounding box.
	const vec3_t angles =
	{
		e->angles[0] * RAD_TO_ANGLE,
		e->angles[1] * RAD_TO_ANGLE * -1.0f,
		e->angles[2] * RAD_TO_ANGLE,
	};

	vec3_t vectors[3];
	AngleVectors(angles, vectors[0], vectors[1], vectors[2]);

	for (int i = 0; i < 8; i++)
	{
		const vec3_t tmp = VEC3_INIT(bbox[i]);

		bbox[i][0] = DotProduct(vectors[0], tmp);
		bbox[i][1] = -DotProduct(vectors[1], tmp);
		bbox[i][2] = DotProduct(vectors[2], tmp);

		Vec3AddAssign(e->origin, bbox[i]);
	}

	int aggregatemask = -1;

	for (int p = 0; p < 8; p++)
	{
		int mask = 0;

		for (int f = 0; f < 4; f++)
		{
			const float dp = DotProduct(frustum[f].normal, bbox[p]);
			if (dp - frustum[f].dist < 0.0f)
				mask |= 1 << f;
		}

		aggregatemask &= mask;
	}

	return aggregatemask != 0;
}

static image_t* R_GetSkin(const entity_t* ent) //mxd. Rewrote to use entity_t* arg instead of 'currententity'.
{
	if (ent->skin != NULL)
		return ent->skin;

	const int skinnum = (ent->skinnum < MAX_FRAMES ? ent->skinnum : 0);
	const model_t* mdl = *ent->model;

	if (mdl->skins[skinnum] != NULL)
		return mdl->skins[skinnum];

	if (mdl->skins[0] != NULL)
		return mdl->skins[0];

	return r_notexture;
}

static image_t* R_GetSkinFromNode(const entity_t* ent, const int skin_index) //mxd. Rewrote to use entity_t* arg instead of 'currententity'.
{
	image_t* skin;
	const model_t* mdl = *ent->model;

	if (ent->skin != NULL && ent->skins != NULL)
		skin = ent->skins[skin_index];
	else
		skin = mdl->skins[skin_index];

	if (skin != NULL)
		return skin;

	if (ent->skin != NULL)
		return ent->skin;

	if (mdl->skins[0] != NULL)
		return mdl->skins[0];

	return r_notexture;
}

static void R_InterpolateVertexNormals(const int num_xyz, const float lerp, const fmtrivertx_t* verts, const fmtrivertx_t* old_verts, vec3_t* normals)
{
	const fmtrivertx_t* v = &verts[0];
	const fmtrivertx_t* ov = &old_verts[0];
	vec3_t* n = &normals[0];

	for (int i = 0; i < num_xyz; i++, v++, ov++, n++)
		VectorLerp(bytedirs[v->lightnormalindex], lerp, bytedirs[ov->lightnormalindex], *n);
}

static void R_EnableReflection(void) //mxd. Added to reduce code duplication.
{
	glEnable(GL_TEXTURE_GEN_S);
	glEnable(GL_TEXTURE_GEN_T);
	glTexGeni(GL_S, GL_TEXTURE_GEN_MODE, GL_SPHERE_MAP);
	glTexGeni(GL_T, GL_TEXTURE_GEN_MODE, GL_SPHERE_MAP);

	R_BindImage(r_reflecttexture);
}

static void R_DrawFlexFrameLerp(const fmdl_t* fmdl, entity_t* e, vec3_t shadelight) //mxd. Original logic uses 'fmodel', 'currententity', 'framelerp' and 'shadelight' global vars.
{
	static vec3_t normals_array[MAX_FM_VERTS]; //mxd. Made static.

	const qboolean draw_reflection = (e->flags & RF_REFLECTION); //mxd. Skipped gl_envmap_broken check.
	const image_t* skin = R_GetSkin(e);
	float alpha = 1.0f; //mxd. Set in Loki Linux version, but not in Windows version.

	if (e->color.a != 255 || e->flags & RF_TRANS_ANY || skin->has_alpha)
	{
		if (e->flags & RF_TRANS_GHOST)
			alpha = shadelight[0] * 0.5f;
		else
			alpha = (float)e->color.a / 255.0f;

		R_HandleTransparency(e);
	}

	if (!(int)r_frameswap->value)
		e->swapFrame = NO_SWAP_FRAME;

	FrameLerp(fmdl, e);

	if (draw_reflection)
	{
		if (fmdl->frames != NULL)
			R_InterpolateVertexNormals(fmdl->header.num_xyz, e->backlerp, sfl_cur_skel.verts, sfl_cur_skel.old_verts, normals_array);

		R_EnableReflection();
	}

	fmnodeinfo_t* nodeinfo = &e->fmnodeinfo[0];
	for (int i = 0; i < fmdl->header.num_mesh_nodes; i++, nodeinfo++)
	{
		qboolean use_color = false;
		qboolean use_skin = false;
		qboolean use_reflect = false;

		if (nodeinfo != NULL)
		{
			if (nodeinfo->flags & FMNI_NO_DRAW)
				continue;

			use_color = (nodeinfo->flags & FMNI_USE_COLOR);
			if (use_color)
			{
				glEnable(GL_BLEND);
				glColor4ub(nodeinfo->color.r, nodeinfo->color.g, nodeinfo->color.b, nodeinfo->color.a);
			}

			if ((nodeinfo->flags & FMNI_USE_REFLECT) && !draw_reflection)
			{
				use_skin = true;
				use_reflect = true;

				R_EnableReflection();
			}
			else if (nodeinfo->flags & FMNI_USE_SKIN)
			{
				use_skin = true;
				R_BindImage(R_GetSkinFromNode(e, nodeinfo->skin));
			}
		}

		int* order = &fmdl->glcmds[fmdl->mesh_nodes[i].start_glcmds];

		while (true)
		{
			// Get the vertex count and primitive type.
			int num_vers = *order++;
			if (num_vers == 0)
				break; // Done.

			if (num_vers < 0)
			{
				num_vers = -num_vers;
				glBegin(GL_TRIANGLE_FAN);
			}
			else
			{
				glBegin(GL_TRIANGLE_STRIP);
			}

			for (int c = 0; c < num_vers; c++)
			{
				const int index_xyz = order[2];

				vec3_t* normal;
				if (fmdl->frames == NULL)
					normal = &bytedirs[fmdl->lightnormalindex[index_xyz]];
				else if (draw_reflection)
					normal = &normals_array[index_xyz];
				else
					normal = &bytedirs[sfl_cur_skel.verts[index_xyz].lightnormalindex];

				glNormal3f((*normal)[0], (*normal)[1], (*normal)[2]);

				// Texture coordinates come from the draw list.
				glTexCoord2f(((float*)order)[0], ((float*)order)[1]);

				order += 3;

				if (!use_color && !(e->flags & RF_FULLBRIGHT))
				{
					float l;
					if (fmdl->frames == NULL)
					{
						l = shadedots[fmdl->lightnormalindex[index_xyz]];
					}
					else
					{
						//mxd. Interpolate light scaler.
						const float cl = shadedots[sfl_cur_skel.verts[index_xyz].lightnormalindex];
						const float ol = shadedots[sfl_cur_skel.old_verts[index_xyz].lightnormalindex];

						l = LerpFloat(cl, ol, e->backlerp);
					}

					glColor4f(l * shadelight[0], l * shadelight[1], l * shadelight[2], alpha);
				}

				glVertex3fv(s_lerped[index_xyz]);
			}

			glEnd();
		}

		if (use_reflect)
		{
			glDisable(GL_TEXTURE_GEN_S);
			glDisable(GL_TEXTURE_GEN_T);
		}

		if (use_skin)
			R_BindImage(skin);
	}

	if (draw_reflection)
	{
		glDisable(GL_TEXTURE_GEN_S);
		glDisable(GL_TEXTURE_GEN_T);
		R_BindImage(skin);
	}

	R_CleanupTransparency(e);
}

//mxd. Somewhat similar to R_DrawAliasModel from Q2. Original code used 'currententity' global var instead of 'e' arg.
void R_DrawFlexModel(entity_t* e)
{
	const fmdl_t* fmdl = (fmdl_t*)(*e->model)->extradata; //mxd. Original code used 'currentmodel' global var here.

	if (R_CullFlexModel(fmdl, e))
		return;

	R_EnsureFlexModelInDXR(e);

	// Get lighting information.
	vec3_t shadelight;
	qboolean apply_minlight = false; //mxd

	if (e->flags & RF_TRANS_ADD_ALPHA)
	{
		const float alpha = (float)e->color.a / 255.0f;
		VectorSet(shadelight, alpha, alpha, alpha);
	}
	else if (e->flags & RF_FULLBRIGHT)
	{
		VectorSet(shadelight, 1.0f, 1.0f, 1.0f);
	}
	else if (e->absLight.r != 0 || e->absLight.g != 0 || e->absLight.b != 0)
	{
		VectorSet(shadelight, (float)e->absLight.r / 255.0f, (float)e->absLight.g / 255.0f, (float)e->absLight.b / 255.0f);
		apply_minlight = true; //mxd
	}
	else if (!(e->flags & RF_GLOW)) //mxd. Skip when result is going to be ignored.
	{
		R_LightPoint(e->origin, shadelight, true); //mxd. Skip RF_WEAPONMODEL logic (never set in H2), skip gl_monolightmap logic.
		apply_minlight = true; //mxd
	}

	// YQ2. Apply minlight?
	if (apply_minlight && gl_state.minlight_set)
	{
		for (int i = 0; i < 3; i++)
		{
			const int ñ = (int)(shadelight[i] * 255.0f);
			shadelight[i] = (float)minlight[min(255, ñ)] / 255.0f;
		}
	}

	for (int i = 0; i < 3; i++)
		shadelight[i] *= (float)e->color.c_array[i] / 255.0f;

	if ((e->flags & RF_MINLIGHT) && shadelight[0] <= 0.1f && shadelight[1] <= 0.1f && shadelight[2] <= 0.1f)
		VectorSet(shadelight, 0.1f, 0.1f, 0.1f);

	if (e->flags & RF_GLOW)
	{
		// Bonus items will pulse with time.
		const float val = sinf(r_newrefdef.time * 7.0f) * 0.3f + 0.7f;
		VectorSet(shadelight, val, val, val);
	}

	shadedots = r_avertexnormal_dots[((int)(e->angles[1] * (SHADEDOT_QUANT / 360.0f * RAD_TO_ANGLE)) & (SHADEDOT_QUANT - 1))];

	// Locate the proper data.
	c_alias_polys += fmdl->header.num_tris;

	// Draw all the triangles.
	if (e->flags & RF_DEPTHHACK) // Hack the depth range to prevent view model from poking into walls.
		glDepthRange(gldepthmin, (gldepthmax - gldepthmin) * 0.3f + gldepthmin);

	glPushMatrix();
	glLoadIdentity();
	R_RotateForEntity(e);

	float entity_matrix[16];
	glGetFloatv(GL_MODELVIEW_MATRIX, entity_matrix);
	glLoadModelMatrixf(entity_matrix);
	glPopMatrix();


	glPushMatrix();
	R_RotateForEntity(e);

	// Select skin.
	R_BindImage(R_GetSkin(e));

	// Draw it.
	glShadeModel(GL_SMOOTH);
	R_TexEnv(GL_MODULATE);

	if (e->frame < 0 || e->frame >= fmdl->header.num_frames)
	{
		e->frame = 0;
		e->oldframe = 0;
	}

	if (e->oldframe < 0 || e->oldframe >= fmdl->header.num_frames)
	{
		ri.Con_Printf(PRINT_ALL, "R_DrawFlexModel: no such oldframe %d\n");
		e->frame = 0;
		e->oldframe = 0;
	}

	if (!(int)r_lerpmodels->value)
		e->backlerp = 0.0f;

	glGeometryFlagf(GEOMETRY_FLAG_SKELETAL);
	R_DrawFlexFrameLerp(fmdl, e, shadelight);
	glGeometryFlagf(GEOMETRY_FLAG_NONE);

	R_TexEnv(GL_REPLACE);
	glShadeModel(GL_FLAT);
	glPopMatrix();

	if (e->flags & RF_TRANS_ANY)
		glDisable(GL_BLEND);

	if (e->flags & RF_DEPTHHACK)
		glDepthRange(gldepthmin, gldepthmax);

	glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

	float identity[16] =
	{
		1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f
	};

	glLoadModelMatrixf(identity);
}

void R_LerpVert(const vec3_t new_point, const vec3_t old_point, vec3_t interpolated_point, const float move[3], const float frontv[3], const float backv[3])
{
	for (int i = 0; i < 3; i++)
		interpolated_point[i] = new_point[i] * frontv[i] + old_point[i] * backv[i] + move[i];
}

#pragma endregion