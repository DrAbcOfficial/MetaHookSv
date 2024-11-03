#pragma once

class CWaterSurfaceModel;

typedef struct epair_s
{
	struct epair_s* next;
	char* key;
	char* value;
} epair_t;

class bspentity_t
{
public:
	~bspentity_t()
	{
		auto pPair = epairs;
		while (pPair)
		{
			auto pFree = pPair;
			pPair = pFree->next;

			delete[] pFree->key;
			delete[] pFree->value;
			delete pFree;
		}
	}

	vec3_t  origin{};
	epair_t* epairs{};
	char* classname{};
};


typedef struct entity_component_s
{
	struct entity_component_s *next;
	std::vector<cl_entity_t *> FollowEnts;
	std::vector<decal_t *> Decals;
	std::vector<CWaterSurfaceModel *> WaterVBOs;
	std::vector<water_reflect_cache_t *> ReflectCaches;
	std::vector<int> DeferredStudioPasses;
}entity_component_t;

entity_component_t *R_GetEntityComponent(cl_entity_t *ent, bool create_if_not_exists);
void R_InitEntityComponents(void);
void R_ShutdownEntityComponents(void);
void R_EntityComponents_PreFrame(void);