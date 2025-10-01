#include "gl_local.h"

#include <sstream>
#include <algorithm>
#include <set>

#include "MurmurHash2.h"
#include "UtilThreadTask.h"
#include "LambdaThreadedTask.h"

CWorldSurfaceRenderer g_WorldSurfaceRenderer;

float r_shadow_matrix[3][16] = { 0 };
float r_world_matrix_inv[16] = { 0 };
float r_projection_matrix_inv[16] = { 0 };

vec3_t r_frustum_origin[4] = { 0 };
vec3_t r_frustum_vec[4] = { 0 };
float r_znear = 4;
float r_zfar = 4096;
bool r_ortho = false;

std::unordered_map <program_state_t, wsurf_program_t> g_WSurfProgramTable;

std::unordered_map <uint32_t, std::shared_ptr<CWorldSurfaceRenderMaterial>> g_WorldTextureRenderMaterials;

std::unordered_map <uint32_t, std::shared_ptr<CWorldSurfaceRenderMaterial>> g_DecalTextureRenderMaterials;

std::vector<std::shared_ptr<CWorldSurfaceModel>> g_WorldSurfaceModels;

std::vector<std::shared_ptr<CWorldSurfaceWorldModel>> g_WorldSurfaceWorldModels;

std::unordered_map<std::string, std::shared_ptr<CWorldSurfaceShadowProxyModel>> g_WorldSurfaceShadowProxyModels;

CWorldSurfaceShadowProxyDraw::~CWorldSurfaceShadowProxyDraw()
{
}

CWorldSurfaceShadowProxyModel::~CWorldSurfaceShadowProxyModel()
{
	if (hEBO)
	{
		GL_DeleteBuffer(hEBO);
		hEBO = 0;
	}

	for (int i = WSURF_VBO_VERTEX; i < WSURF_VBO_MAX; ++i)
	{
		if (hVBO[i])
		{
			GL_DeleteBuffer(hVBO[i]);
			hVBO[i] = 0;
		}
	}

	if (hVAO)
	{
		GL_DeleteVAO(hVAO);
		hVAO = 0;
	}

	if (hABO)
	{
		GL_DeleteBuffer(hABO);
		hABO = 0;
	}
}

CWorldSurfaceLeaf::~CWorldSurfaceLeaf()
{
	if (hABO)
	{
		GL_DeleteBuffer(hABO);
		hABO = 0;
	}

	m_vWaterSurfaceModels.clear();
}

CWorldSurfaceWorldModel::~CWorldSurfaceWorldModel()
{
	if (hEBO)
	{
		GL_DeleteBuffer(hEBO);
		hEBO = 0;
	}

	for (int i = WSURF_VBO_VERTEX; i < WSURF_VBO_MAX; ++i)
	{
		if (hVBO[i])
		{
			GL_DeleteBuffer(hVBO[i]);
			hVBO[i] = 0;
		}
	}

	if (hVAO)
	{
		GL_DeleteVAO(hVAO);
		hVAO = 0;
	}
}

void R_FreeWorldSurfaceModels(model_t* mod)
{
	int modelindex = EngineGetModelIndex(mod);

	if (modelindex >= 0 && modelindex < g_WorldSurfaceModels.size())
	{
		auto pWorldSurfaceModel = g_WorldSurfaceModels[modelindex];

		if (pWorldSurfaceModel)
		{
			gEngfuncs.Con_DPrintf("R_FreeWorldSurfaceModels: [%s] freed.\n", mod->name);

			pWorldSurfaceModel->FreeLeaves();

			g_WorldSurfaceModels[modelindex].reset();
		}
	}
}

void R_FreeWorldSurfaceWorldModels(model_t* mod)
{
	int modelindex = EngineGetModelIndex(mod);

	if (modelindex >= 0 && modelindex < g_WorldSurfaceWorldModels.size())
	{
		auto pWorldSurfaceWorldModel = g_WorldSurfaceWorldModels[modelindex];

		if (pWorldSurfaceWorldModel)
		{
			gEngfuncs.Con_DPrintf("R_FreeWorldSurfaceWorldModels: [%s] freed.\n", mod->name);

			g_WorldSurfaceWorldModels[modelindex].reset();
		}
	}
}

void R_ClearWorldSurfaceModels(void)
{
	for (size_t i = 0; i < g_WorldSurfaceModels.size(); ++i)
	{
		auto pWorldSurfaceModel = g_WorldSurfaceModels[i];

		if (pWorldSurfaceModel)
		{
			pWorldSurfaceModel->FreeLeaves();

			g_WorldSurfaceModels[i].reset();
		}
	}
}

void R_ClearWorldSurfaceWorldModels(void)
{
	for (size_t i = 0; i < g_WorldSurfaceWorldModels.size(); ++i)
	{
		auto pWorldSurfaceWorldModel = g_WorldSurfaceWorldModels[i];

		if (pWorldSurfaceWorldModel)
		{
			g_WorldSurfaceWorldModels[i].reset();
		}
	}
}

const program_state_mapping_t s_WSurfProgramStateName[] = {
{ WSURF_DIFFUSE_ENABLED				,"WSURF_DIFFUSE_ENABLED"},
{ WSURF_LIGHTMAP_ENABLED			,"WSURF_LIGHTMAP_ENABLED"},
{ WSURF_DETAILTEXTURE_ENABLED		,"WSURF_DETAILTEXTURE_ENABLED"},
{ WSURF_NORMALTEXTURE_ENABLED		,"WSURF_NORMALTEXTURE_ENABLED"},
{ WSURF_PARALLAXTEXTURE_ENABLED		,"WSURF_PARALLAXTEXTURE_ENABLED"},
{ WSURF_SPECULARTEXTURE_ENABLED		,"WSURF_SPECULARTEXTURE_ENABLED"},
{ WSURF_LINEAR_FOG_ENABLED			,"WSURF_LINEAR_FOG_ENABLED"},
{ WSURF_EXP_FOG_ENABLED				,"WSURF_EXP_FOG_ENABLED"},
{ WSURF_EXP2_FOG_ENABLED			,"WSURF_EXP2_FOG_ENABLED"},
{ WSURF_GBUFFER_ENABLED				,"WSURF_GBUFFER_ENABLED"},
{ WSURF_SHADOW_CASTER_ENABLED		,"WSURF_SHADOW_CASTER_ENABLED"},
{ WSURF_SKYBOX_ENABLED				,"WSURF_SKYBOX_ENABLED"},
{ WSURF_DECAL_ENABLED				,"WSURF_DECAL_ENABLED"},
{ WSURF_CLIP_ENABLED				,"WSURF_CLIP_ENABLED"},
{ WSURF_CLIP_WATER_ENABLED			,"WSURF_CLIP_WATER_ENABLED"},
{ WSURF_ALPHA_BLEND_ENABLED			,"WSURF_ALPHA_BLEND_ENABLED"},
{ WSURF_ADDITIVE_BLEND_ENABLED		,"WSURF_ADDITIVE_BLEND_ENABLED"},
{ WSURF_OIT_BLEND_ENABLED			,"WSURF_OIT_BLEND_ENABLED"},
{ WSURF_GAMMA_BLEND_ENABLED			,"WSURF_GAMMA_BLEND_ENABLED"},
{ WSURF_FULLBRIGHT_ENABLED			,"WSURF_FULLBRIGHT_ENABLED"},
{ WSURF_COLOR_FILTER_ENABLED		,"WSURF_COLOR_FILTER_ENABLED"},
{ WSURF_LIGHTMAP_INDEX_0_ENABLED	,"WSURF_LIGHTMAP_INDEX_0_ENABLED"},
{ WSURF_LIGHTMAP_INDEX_1_ENABLED	,"WSURF_LIGHTMAP_INDEX_1_ENABLED"},
{ WSURF_LIGHTMAP_INDEX_2_ENABLED	,"WSURF_LIGHTMAP_INDEX_2_ENABLED"},
{ WSURF_LIGHTMAP_INDEX_3_ENABLED	,"WSURF_LIGHTMAP_INDEX_3_ENABLED"},
{ WSURF_LEGACY_DLIGHT_ENABLED		,"WSURF_LEGACY_DLIGHT_ENABLED"},
{ WSURF_ALPHA_SOLID_ENABLED			,"WSURF_ALPHA_SOLID_ENABLED"},
{ WSURF_LINEAR_FOG_SHIFT_ENABLED	,"WSURF_LINEAR_FOG_SHIFT_ENABLED"},
{ WSURF_REVERT_NORMAL_ENABLED		,"WSURF_REVERT_NORMAL_ENABLED"},
};

void R_SaveWSurfProgramStates(void)
{
	std::vector<program_state_t> states;
	for (auto& p : g_WSurfProgramTable)
	{
		states.emplace_back(p.first);
	}
	R_SaveProgramStatesCaches("renderer/shader/wsurf_cache.txt", states, s_WSurfProgramStateName, _ARRAYSIZE(s_WSurfProgramStateName));
}

void R_LoadWSurfProgramStates(void)
{
	R_LoadProgramStateCaches("renderer/shader/wsurf_cache.txt", s_WSurfProgramStateName, _ARRAYSIZE(s_WSurfProgramStateName), [](program_state_t state) {

		R_UseWSurfProgram(state, NULL);

		});
}

void R_UseWSurfProgram(program_state_t state, wsurf_program_t* progOutput)
{
	wsurf_program_t prog = { 0 };

	auto itor = g_WSurfProgramTable.find(state);
	if (itor == g_WSurfProgramTable.end())
	{
		std::stringstream defs;

		if (state & WSURF_DIFFUSE_ENABLED)
			defs << "#define DIFFUSE_ENABLED\n";

		if (state & WSURF_LIGHTMAP_ENABLED)
			defs << "#define LIGHTMAP_ENABLED\n";

		if (state & WSURF_DETAILTEXTURE_ENABLED)
			defs << "#define DETAILTEXTURE_ENABLED\n";

		if (state & WSURF_NORMALTEXTURE_ENABLED)
			defs << "#define NORMALTEXTURE_ENABLED\n";

		if (state & WSURF_PARALLAXTEXTURE_ENABLED)
			defs << "#define PARALLAXTEXTURE_ENABLED\n";

		if (state & WSURF_SPECULARTEXTURE_ENABLED)
			defs << "#define SPECULARTEXTURE_ENABLED\n";

		if (state & WSURF_LINEAR_FOG_ENABLED)
			defs << "#define LINEAR_FOG_ENABLED\n";

		if (state & WSURF_EXP_FOG_ENABLED)
			defs << "#define EXP_FOG_ENABLED\n";

		if (state & WSURF_EXP2_FOG_ENABLED)
			defs << "#define EXP2_FOG_ENABLED\n";

		if (state & WSURF_GBUFFER_ENABLED)
			defs << "#define GBUFFER_ENABLED\n";

		if (state & WSURF_SHADOW_CASTER_ENABLED)
			defs << "#define SHADOW_CASTER_ENABLED\n";

		if (state & WSURF_SKYBOX_ENABLED)
			defs << "#define SKYBOX_ENABLED\n";

		if (state & WSURF_DECAL_ENABLED)
			defs << "#define DECAL_ENABLED\n";

		if (state & WSURF_CLIP_ENABLED)
			defs << "#define CLIP_ENABLED\n";

		if (state & WSURF_CLIP_WATER_ENABLED)
			defs << "#define CLIP_WATER_ENABLED\n";

		if (state & WSURF_ALPHA_BLEND_ENABLED)
			defs << "#define ALPHA_BLEND_ENABLED\n";

		if (state & WSURF_ADDITIVE_BLEND_ENABLED)
			defs << "#define ADDITIVE_BLEND_ENABLED\n";

		if ((state & WSURF_OIT_BLEND_ENABLED) && g_bUseOITBlend)
			defs << "#define OIT_BLEND_ENABLED\n";

		if (state & WSURF_GAMMA_BLEND_ENABLED)
			defs << "#define GAMMA_BLEND_ENABLED\n";

		if (state & WSURF_FULLBRIGHT_ENABLED)
			defs << "#define FULLBRIGHT_ENABLED\n";

		if (state & WSURF_COLOR_FILTER_ENABLED)
			defs << "#define COLOR_FILTER_ENABLED\n";

		if (state & WSURF_LIGHTMAP_INDEX_0_ENABLED)
			defs << "#define LIGHTMAP_INDEX_0_ENABLED\n";

		if (state & WSURF_LIGHTMAP_INDEX_1_ENABLED)
			defs << "#define LIGHTMAP_INDEX_1_ENABLED\n";

		if (state & WSURF_LIGHTMAP_INDEX_2_ENABLED)
			defs << "#define LIGHTMAP_INDEX_2_ENABLED\n";

		if (state & WSURF_LIGHTMAP_INDEX_3_ENABLED)
			defs << "#define LIGHTMAP_INDEX_3_ENABLED\n";

		if (state & WSURF_LEGACY_DLIGHT_ENABLED)
			defs << "#define LEGACY_DLIGHT_ENABLED\n";

		if (state & WSURF_ALPHA_SOLID_ENABLED)
			defs << "#define ALPHA_SOLID_ENABLED\n";

		if (state & WSURF_LINEAR_FOG_SHIFT_ENABLED)
			defs << "#define LINEAR_FOG_SHIFT_ENABLED\n";

		if (state & WSURF_REVERT_NORMAL_ENABLED)
			defs << "#define REVERT_NORMAL_ENABLED\n";

		auto def = defs.str();

		CCompileShaderArgs args;
		args.vsfile = "renderer\\shader\\wsurf_shader.vert.glsl";
		args.fsfile = "renderer\\shader\\wsurf_shader.frag.glsl";
		args.vsdefine = def.c_str();
		args.fsdefine = def.c_str();

		prog.program = R_CompileShaderFileEx(&args);
		
		if (prog.program)
		{
			SHADER_UNIFORM(prog, u_parallaxScale, "u_parallaxScale");
		}

		g_WSurfProgramTable[state] = prog;
	}
	else
	{
		prog = itor->second;
	}

	if (prog.program)
	{
		GL_UseProgram(prog.program);

		if (prog.u_parallaxScale != -1)
			glUniform1f(prog.u_parallaxScale, r_wsurf_parallax_scale->value);

		if (progOutput)
			*progOutput = prog;
	}
	else
	{
		g_pMetaHookAPI->SysError("R_UseWSurfProgram: Failed to load program!");
	}
}

void R_FreeSceneUBO(void)
{
	if (g_WorldSurfaceRenderer.hDecalVAO)
	{
		GL_DeleteVAO(g_WorldSurfaceRenderer.hDecalVAO);
		g_WorldSurfaceRenderer.hDecalVAO = 0;
	}

	for (int i = 0; i < WSURF_VBO_MAX; ++i)
	{
		if (g_WorldSurfaceRenderer.hDecalVBO[i])
		{
			GL_DeleteBuffer(g_WorldSurfaceRenderer.hDecalVBO[i]);
			g_WorldSurfaceRenderer.hDecalVBO[i] = 0;
		}
	}

	if (g_WorldSurfaceRenderer.hDecalEBO)
	{
		GL_DeleteBuffer(g_WorldSurfaceRenderer.hDecalEBO);
		g_WorldSurfaceRenderer.hDecalEBO = 0;
	}

	if (g_WorldSurfaceRenderer.hSceneUBO)
	{
		GL_DeleteBuffer(g_WorldSurfaceRenderer.hSceneUBO);
		g_WorldSurfaceRenderer.hSceneUBO = 0;
	}

	if (g_WorldSurfaceRenderer.hCameraUBO)
	{
		GL_DeleteBuffer(g_WorldSurfaceRenderer.hCameraUBO);
		g_WorldSurfaceRenderer.hCameraUBO = 0;
	}

	if (g_WorldSurfaceRenderer.hEntityUBO)
	{
		GL_DeleteBuffer(g_WorldSurfaceRenderer.hEntityUBO);
		g_WorldSurfaceRenderer.hEntityUBO = 0;
	}

	if (g_WorldSurfaceRenderer.hDLightUBO)
	{
		GL_DeleteBuffer(g_WorldSurfaceRenderer.hDLightUBO);
		g_WorldSurfaceRenderer.hDLightUBO = 0;
	}

	if (g_WorldSurfaceRenderer.hMaterialSSBO)
	{
		GL_DeleteBuffer(g_WorldSurfaceRenderer.hMaterialSSBO);
		g_WorldSurfaceRenderer.hMaterialSSBO = 0;
	}

	if (g_WorldSurfaceRenderer.hOITFragmentSSBO)
	{
		GL_DeleteBuffer(g_WorldSurfaceRenderer.hOITFragmentSSBO);
		g_WorldSurfaceRenderer.hOITFragmentSSBO = 0;
	}

	if (g_WorldSurfaceRenderer.hOITNumFragmentSSBO)
	{
		GL_DeleteBuffer(g_WorldSurfaceRenderer.hOITNumFragmentSSBO);
		g_WorldSurfaceRenderer.hOITNumFragmentSSBO = 0;
	}

	if (g_WorldSurfaceRenderer.hOITAtomicSSBO)
	{
		GL_DeleteBuffer(g_WorldSurfaceRenderer.hOITAtomicSSBO);
		g_WorldSurfaceRenderer.hOITAtomicSSBO = 0;
	}
}

void R_FreeLightmapTextures()
{
	for (int lightmap_idx = 0; lightmap_idx < MAXLIGHTMAPS; ++lightmap_idx)
	{
		if (g_WorldSurfaceRenderer.iLightmapTextureArray[lightmap_idx])
		{
			gEngfuncs.Con_DPrintf("R_FreeLightmapTextures: delete texid [%d].\n", g_WorldSurfaceRenderer.iLightmapTextureArray);
			GL_DeleteTexture(g_WorldSurfaceRenderer.iLightmapTextureArray[lightmap_idx]);
			g_WorldSurfaceRenderer.iLightmapTextureArray[lightmap_idx] = 0;
		}
	}
}

using visnodes_t = std::set<mbasenode_t*>;
using vissurfaces_t = std::set<msurface_t*>;
using texsurfaces_t = std::vector<msurface_t*>;

void R_RecursiveFindLeaves(mbasenode_t* basenode, std::set<mleaf_t*>& vLeafs)
{
	if (basenode->contents < 0)
	{
		auto pleaf = (mleaf_t*)basenode;

		vLeafs.emplace(pleaf);
		return;
	}

	auto node = (mnode_t*)basenode;

	R_RecursiveFindLeaves(node->children[0], vLeafs);

	R_RecursiveFindLeaves(node->children[1], vLeafs);
}

void R_MarkPVSForLeaf(model_t * worldmodel, mleaf_t* leaf, visnodes_t &visnodes)
{
	//Decompress vis bytes from world model.

	byte vis[MAX_MAP_LEAFS_SVENGINE / 8];

	Mod_LeafPVS(leaf, worldmodel, vis);

	//Mark node as visnode, from leaf to root
	for (int i = 0; i < worldmodel->numleafs; i++)
	{
		if ((byte)(1 << (i & 7)) & vis[i >> 3])
		{
			auto basenode = (mbasenode_t*)&worldmodel->leafs[i + 1];

			do
			{
				visnodes.emplace(basenode);

				basenode = basenode->parent;

			} while (basenode);
		}
	}
}

void R_RecursiveMarkSurfaces(mbasenode_t* basenode, const visnodes_t& visnodes, vissurfaces_t& vissurfaces)
{
	if (basenode->contents == CONTENTS_SOLID)
		return;

	if (visnodes.find(basenode) == visnodes.end())
		return;

	if (basenode->contents < 0)
	{
		auto leaf = (mleaf_t*)basenode;

		auto marks = leaf->firstmarksurface;
		auto nummarks = leaf->nummarksurfaces;

		for (int i = 0; i < nummarks; ++i)
		{
			vissurfaces.emplace(marks[i]);
		}
		return;
	}

	auto node = (mnode_t*)basenode;

	R_RecursiveMarkSurfaces(node->children[0], visnodes, vissurfaces);

	R_RecursiveMarkSurfaces(node->children[1], visnodes, vissurfaces);
}

texture_t* R_GetEmptyWorldTexture()
{
	if (g_iEngineType == ENGINE_SVENGINE)
		return (*r_missingtexture);

	return (*r_notexture_mip);
}

void R_RecursiveLinkTextureChain(model_t* mod, mbasenode_t* basenode, const visnodes_t& visnodes, const vissurfaces_t& vissurfaces, vissurfaces_t& watersurfaces, texsurfaces_t *texsurfaces)
{
	if (basenode->contents == CONTENTS_SOLID)
		return;

	if (visnodes.find(basenode) == visnodes.end())
		return;

	if (basenode->contents < 0)
		return;

	auto node = (mnode_t*)basenode;

	R_RecursiveLinkTextureChain(mod, node->children[0], visnodes, vissurfaces, watersurfaces, texsurfaces);

	for (int i = 0; i < node->numsurfaces; ++i)
	{
		auto surf = R_GetWorldSurfaceByIndex(mod, node->firstsurface + i);

		if (vissurfaces.find(surf) == vissurfaces.end())
		{
			continue;
		}

		if (surf->flags & SURF_DRAWTURB)
		{
			watersurfaces.emplace(surf);
			continue;
		}

		auto ptexture = (surf->texinfo && surf->texinfo->texture) ? surf->texinfo->texture : nullptr;

		if (ptexture)
		{
			auto textureIndex = R_FindTextureIdByTexture(mod, ptexture);

			if (textureIndex >= 0 && textureIndex < mod->numtextures)
			{
				texsurfaces[textureIndex].emplace_back(surf);
			}
			else
			{
				texsurfaces[mod->numtextures].emplace_back(surf);
			}
		}
		else
		{
			texsurfaces[mod->numtextures].emplace_back(surf);
		}
	}

	R_RecursiveLinkTextureChain(mod, node->children[1], visnodes, vissurfaces, watersurfaces, texsurfaces);
}

void R_BrushModelLinkTextureChain(model_t* mod, vissurfaces_t& watersurfaces, vissurfaces_t& reversedwatersurfaces, texsurfaces_t* texsurfaces)
{
	for (int i = 0; i < mod->nummodelsurfaces; i++)
	{
		auto surf = R_GetWorldSurfaceByIndex(mod, mod->firstmodelsurface + i);

		auto pplane = surf->plane;

		if (surf->flags & SURF_DRAWTURB)
		{
			//Skip non-Z planes
			if (pplane->type != PLANE_Z)
				continue;

			watersurfaces.emplace(surf);
			reversedwatersurfaces.emplace(surf);
			continue;
		}

		auto ptexture = (surf->texinfo && surf->texinfo->texture) ? surf->texinfo->texture : nullptr;

		if (ptexture)
		{
			auto textureIndex = R_FindTextureIdByTexture(mod, ptexture);

			if (textureIndex >= 0 && textureIndex < mod->numtextures)
			{
				texsurfaces[textureIndex].emplace_back(surf);
			}
			else
			{
				texsurfaces[mod->numtextures].emplace_back(surf);
			}
		}
		else
		{
			texsurfaces[mod->numtextures].emplace_back(surf);
		}
	}
}

void R_GenerateIndicesForTexChain(model_t* mod, msurface_t* surf, CWorldSurfaceBrushTexChain* texchain, CWorldSurfaceWorldModel* pWorldModel, CWorldSurfaceLeaf *pLeaf, std::vector<CDrawIndexAttrib>& vDrawAttribBuffer)
{
	if (pLeaf->m_bIsClosing.load())
		return; //Leaf is closing, stop generating texchain

	auto surfIndex = R_GetWorldSurfaceIndex(pWorldModel->m_model, surf);

	if (surfIndex == -1)
	{
		Sys_Error("R_GenerateIndicesForTexChain: invalid surfIndex!");
		return;
	}

	const auto& brushface = pWorldModel->m_vFaceBuffer[surfIndex];

	if (surf->flags & SURF_DRAWSKY)
	{
		if (texchain->type == TEXCHAIN_SKY)
		{
			CDrawIndexAttrib drawAttrib;

			drawAttrib.FirstIndexLocation = brushface.start_index;
			drawAttrib.NumIndices = brushface.index_count;
			drawAttrib.FirstInstanceLocation = brushface.instance_index;
			drawAttrib.NumInstances = brushface.instance_count;

			vDrawAttribBuffer.emplace_back(drawAttrib);

			texchain->drawCount++;
			texchain->polyCount += brushface.poly_count;
		}
	}
	else if (surf->flags & SURF_DRAWTURB)
	{

	}
	else if (surf->flags & SURF_UNDERWATER)
	{

	}
	else if (surf->flags & SURF_DRAWTILED)
	{
		if (texchain->type == TEXCHAIN_SCROLL)
		{
			CDrawIndexAttrib drawAttrib;

			drawAttrib.FirstIndexLocation = brushface.start_index;
			drawAttrib.NumIndices = brushface.index_count;
			drawAttrib.FirstInstanceLocation = brushface.instance_index;
			drawAttrib.NumInstances = brushface.instance_count;

			vDrawAttribBuffer.emplace_back(drawAttrib);

			texchain->drawCount++;
			texchain->polyCount += brushface.poly_count;
		}
	}
	else
	{
		if (texchain->type == TEXCHAIN_STATIC)
		{
			CDrawIndexAttrib drawAttrib;

			drawAttrib.FirstIndexLocation = brushface.start_index;
			drawAttrib.NumIndices = brushface.index_count;
			drawAttrib.FirstInstanceLocation = brushface.instance_index;
			drawAttrib.NumInstances = brushface.instance_count;

			vDrawAttribBuffer.emplace_back(drawAttrib);

			texchain->drawCount++;
			texchain->polyCount += brushface.poly_count;
		}
	}
}

void R_GenerateResourceForWaterModels(model_t* mod, CWorldSurfaceWorldModel* pWorldModel, CWorldSurfaceLeaf* pLeaf)
{
	for (size_t i = 0; i < pLeaf->m_vWaterSurfaceModels.size(); ++i)
	{
		auto pWaterModel = pLeaf->m_vWaterSurfaceModels[i];

		if (pWaterModel->m_vDrawAttribBuffer.size() > 0)
		{
			pWaterModel->hABO = GL_GenBuffer();

			GL_UploadDataToABOStaticDraw(pWaterModel->hABO, sizeof(CDrawIndexAttrib) * pWaterModel->m_vDrawAttribBuffer.size(), pWaterModel->m_vDrawAttribBuffer.data());

			pWaterModel->drawCount = pWaterModel->m_vDrawAttribBuffer.size();
		}
	}
}

void R_GenerateTexChain(model_t* mod, const texsurfaces_t* texsurfaces, CWorldSurfaceWorldModel* pWorldModel, CWorldSurfaceLeaf* pLeaf, int iTexChainPass, std::vector<CDrawIndexAttrib>& vDrawAttribBuffer)
{
	for (int i = 0; i < mod->numtextures + 1; i++)
	{
		texture_t* t = nullptr;
		
		if (i == mod->numtextures)
			t = R_GetEmptyWorldTexture();
		else
			t = mod->textures[i];

		if (!t)
			continue;

		if (pLeaf->m_bIsClosing.load())
			return; //Leaf is closing, stop generating texchain

		bool bIsSkyTexture = (0 == strcmp(t->name, "sky")) ? true : false;

		if (iTexChainPass == TEXCHAIN_PASS_SOLID_WITH_SKY && bIsSkyTexture)
		{
			const auto& surfaces = texsurfaces[i];

			if (surfaces.size() > 0)
			{
				CWorldSurfaceBrushTexChain texchain;

				texchain.type = TEXCHAIN_SKY;
				texchain.texture = t;
				texchain.pRenderMaterial = R_GetRenderMaterialForWorldTexture(t);
				texchain.drawCount = 0;
				texchain.polyCount = 0;
				texchain.startDrawOffset = (uint32_t)vDrawAttribBuffer.size() * sizeof(CDrawIndexAttrib);

				for (const auto& surf : surfaces)
				{
					R_GenerateIndicesForTexChain(mod, surf, &texchain, pWorldModel, pLeaf, vDrawAttribBuffer);
				}

				if (texchain.drawCount > 0)
					pLeaf->TextureChainSpecial[WSURF_TEXCHAIN_SPECIAL_SKY] = texchain;
			}

			//End construction
		}
		else if (iTexChainPass == TEXCHAIN_PASS_SOLID && t->anim_total && !bIsSkyTexture)
		{
			if (t->name[0] == '-')
			{
				//Construct texchain for random textures
				const auto& surfaces = texsurfaces[i];

				if (surfaces.size() > 0)
				{			
					int numtexturechain = surfaces.size();

					//rtable not initialized?
					if ((*rtable)[0][0] == 0)
					{
						for (auto tu = 0; tu < 20; tu++)
						{
							for (auto tv = 0; tv < 20; tv++)
							{
								(*rtable)[tu][tv] = gEngfuncs.pfnRandomLong(0, 0x7FFF);
							}
						}
					}

					int* texchainMapper = new int[numtexturechain];
					msurface_t** texchainSurface = new msurface_t * [numtexturechain];

					//Shuffle surfaces to texchainSurface
					{
						for (size_t k = 0; k < surfaces.size(); ++k)
						{
							auto s2 = surfaces[k];

							texchainSurface[k] = s2;

							int mappingIndex = (*rtable)[(int)((s2->texturemins[0] + (t->width << 16)) / t->width) % 20][(int)((s2->texturemins[1] + (t->height << 16)) / t->height) % 20] % t->anim_total;

							texchainMapper[k] = mappingIndex;
						}
					}

					{
						texture_t* t2 = t;
						for (int k = 0; k < t->anim_total && t2; t2 = t2->anim_next, ++k)
						{
							CWorldSurfaceBrushTexChain texchain;
							texchain.type = TEXCHAIN_STATIC;
							texchain.texture = t2;
							texchain.pRenderMaterial = R_GetRenderMaterialForWorldTexture(t2);
							texchain.drawCount = 0;
							texchain.polyCount = 0;
							texchain.startDrawOffset = (uint32_t)vDrawAttribBuffer.size() * sizeof(CDrawIndexAttrib);

							for (int n = 0; n < numtexturechain; ++n)
							{
								if (texchainMapper[n] == k)
								{
									auto surf = texchainSurface[n];

									if (!(surf->flags & (SURF_DRAWSKY | SURF_DRAWTURB)))
									{
										R_GenerateIndicesForTexChain(mod, texchainSurface[n], &texchain, pWorldModel, pLeaf, vDrawAttribBuffer);
									}
								}
							}

							if (texchain.drawCount > 0)
								pLeaf->vTextureChainList[WSURF_TEXCHAIN_LIST_STATIC].emplace_back(texchain);
						}
					}

					delete[]texchainSurface;
					delete[]texchainMapper;
				}

				//End construction
			}
			else if (t->name[0] == '+')
			{
				//Construct texchain for anim textures

				const auto& surfaces = texsurfaces[i];

				if (surfaces.size() > 0)
				{
					CWorldSurfaceBrushTexChain texchain;

					texchain.type = TEXCHAIN_STATIC;
					texchain.texture = t;
					texchain.pRenderMaterial = R_GetRenderMaterialForWorldTexture(t);
					texchain.drawCount = 0;
					texchain.polyCount = 0;
					texchain.startDrawOffset = (uint32_t)vDrawAttribBuffer.size() * sizeof(CDrawIndexAttrib);

					for (const auto& surf : surfaces)
					{
						if (!(surf->flags & (SURF_DRAWSKY | SURF_DRAWTURB)))
						{
							R_GenerateIndicesForTexChain(mod, surf, &texchain, pWorldModel, pLeaf, vDrawAttribBuffer);
						}
					}

					if (texchain.drawCount > 0)
						pLeaf->vTextureChainList[WSURF_TEXCHAIN_LIST_ANIM].emplace_back(texchain);
				}

				//End construction
			}
		}
		else if (iTexChainPass == TEXCHAIN_PASS_SOLID && !bIsSkyTexture)
		{
			//Construct texchain for static textures

			const auto& surfaces = texsurfaces[i];

			if (surfaces.size() > 0)
			{
				CWorldSurfaceBrushTexChain texchain;

				texchain.type = TEXCHAIN_STATIC;
				texchain.texture = t;
				texchain.pRenderMaterial = R_GetRenderMaterialForWorldTexture(t);
				texchain.drawCount = 0;
				texchain.polyCount = 0;
				texchain.startDrawOffset = (uint32_t)vDrawAttribBuffer.size() * sizeof(CDrawIndexAttrib);

				for (const auto& surf : surfaces)
				{
					if (!(surf->flags & (SURF_DRAWSKY | SURF_DRAWTURB)))
					{
						R_GenerateIndicesForTexChain(mod, surf, &texchain, pWorldModel, pLeaf, vDrawAttribBuffer);
					}
				}

				if (texchain.drawCount > 0)
					pLeaf->vTextureChainList[WSURF_TEXCHAIN_LIST_STATIC].emplace_back(texchain);
			}

			//Construct texchain for scroll textures

			if (surfaces.size() > 0)
			{
				CWorldSurfaceBrushTexChain texchain;

				texchain.type = TEXCHAIN_SCROLL;
				texchain.texture = t;
				texchain.pRenderMaterial = R_GetRenderMaterialForWorldTexture(t);
				texchain.drawCount = 0;
				texchain.polyCount = 0;
				texchain.startDrawOffset = (uint32_t)vDrawAttribBuffer.size() * sizeof(CDrawIndexAttrib);

				for (const auto& surf : surfaces)
				{
					if (!(surf->flags & (SURF_DRAWSKY | SURF_DRAWTURB)))
					{
						R_GenerateIndicesForTexChain(mod, surf, &texchain, pWorldModel, pLeaf, vDrawAttribBuffer);
					}
				}

				if (texchain.drawCount > 0)
					pLeaf->vTextureChainList[WSURF_TEXCHAIN_LIST_STATIC].emplace_back(texchain);
			}

			//End construction

		}
	}

	if (iTexChainPass == TEXCHAIN_PASS_SOLID)
	{
		CWorldSurfaceBrushTexChain texchain;

		texchain.type = TEXCHAIN_STATIC;
		texchain.texture = nullptr;
		texchain.pRenderMaterial = nullptr;
		texchain.drawCount = (uint32_t)vDrawAttribBuffer.size();
		texchain.polyCount = 0;
		texchain.startDrawOffset = 0;

		pLeaf->TextureChainSpecial[WSURF_TEXCHAIN_SPECIAL_SOLID] = texchain;
	}
	else if (iTexChainPass == TEXCHAIN_PASS_SOLID_WITH_SKY)
	{
		CWorldSurfaceBrushTexChain texchain;

		texchain.type = TEXCHAIN_STATIC;
		texchain.texture = nullptr;
		texchain.pRenderMaterial = nullptr;
		texchain.drawCount = (uint32_t)vDrawAttribBuffer.size();
		texchain.polyCount = 0;
		texchain.startDrawOffset = 0;

		pLeaf->TextureChainSpecial[WSURF_TEXCHAIN_SPECIAL_SOLID_WITH_SKY] = texchain;
	}
}

class CWorldSurfaceLeafAsyncLoadTask : public CGameResourceAsyncLoadTask
{
private:
	std::shared_ptr<CWorldSurfaceLeaf> m_pLeaf;
	std::shared_ptr<CWorldSurfaceWorldModel> m_pWorldModel;
	model_t* m_model{};
	mleaf_t* m_leaf{};
	bool m_bIsWorldLeaf{};

	visnodes_t m_visnodes;
	vissurfaces_t m_vissurfaces;
	vissurfaces_t m_watersurfaces;
	vissurfaces_t m_reversedwatersurfaces;
	texsurfaces_t* m_texsurfaces{};

	std::vector<CDrawIndexAttrib> m_vDrawAttribBuffer;
public:
	CWorldSurfaceLeafAsyncLoadTask(
		const std::shared_ptr<CWorldSurfaceLeaf>& pLeaf,
		const std::shared_ptr<CWorldSurfaceWorldModel>& pWorldModel,
		model_t* mod,
		mleaf_t *leaf,
		bool bIsWorldLeaf)
		:
		m_pLeaf(pLeaf),
		m_pWorldModel(pWorldModel),
		m_model(mod),
		m_leaf(leaf),
		m_bIsWorldLeaf(bIsWorldLeaf)
	{
		//m_texsurfaces[m_model->numtextures] is empty texture
		m_texsurfaces = new texsurfaces_t[m_model->numtextures + 1];

		m_hThreadWorkItem = g_pMetaHookAPI->CreateWorkItem(g_pMetaHookAPI->GetGlobalThreadPool(), [](void* context) {

			auto ctx = (CWorldSurfaceLeafAsyncLoadTask*)context;

			std::shared_ptr<CWorldSurfaceLeaf> pLeaf = ctx->m_pLeaf;

			if (ctx->RunTask())
			{
				GameThreadTaskScheduler()->QueueTask(LambdaThreadedTask_CreateInstance([pLeaf]() {

					pLeaf->AsyncUploadResouce();
					pLeaf->ReleaseAsyncLoadTask();

					}));
			}
			else
			{
				GameThreadTaskScheduler()->QueueTask(LambdaThreadedTask_CreateInstance([pLeaf]() {

					pLeaf->ReleaseAsyncLoadTask();

					}));
			}

			return false;

			}, this);
	}

	~CWorldSurfaceLeafAsyncLoadTask()
	{
		if (m_texsurfaces)
		{
			delete[]m_texsurfaces;
			m_texsurfaces = nullptr;
		}
	}

	void StartAsyncTask() override
	{
		g_pMetaHookAPI->QueueWorkItem(g_pMetaHookAPI->GetGlobalThreadPool(), m_hThreadWorkItem);
	}

	bool RunTask() override
	{
		if (m_pLeaf->m_bIsClosing.load())
			return false;

		if (m_bIsWorldLeaf)
		{
			R_MarkPVSForLeaf(m_pWorldModel->m_model, m_leaf, m_visnodes);

			R_RecursiveMarkSurfaces(m_model->nodes, m_visnodes, m_vissurfaces);

			R_RecursiveLinkTextureChain(m_model, m_model->nodes, m_visnodes, m_vissurfaces, m_watersurfaces, m_texsurfaces);

			R_GenerateTexChain(m_model, m_texsurfaces, m_pWorldModel.get(), m_pLeaf.get(), TEXCHAIN_PASS_SOLID, m_vDrawAttribBuffer);

			if (m_pLeaf->m_bIsClosing.load())
				return false;

			R_GenerateTexChain(m_model, m_texsurfaces, m_pWorldModel.get(), m_pLeaf.get(), TEXCHAIN_PASS_SOLID_WITH_SKY, m_vDrawAttribBuffer);
		}
		else
		{
			R_BrushModelLinkTextureChain(m_model, m_watersurfaces, m_reversedwatersurfaces, m_texsurfaces);

			R_GenerateTexChain(m_model, m_texsurfaces, m_pWorldModel.get(), m_pLeaf.get(), TEXCHAIN_PASS_SOLID, m_vDrawAttribBuffer);

			if (m_pLeaf->m_bIsClosing.load())
				return false;

			R_GenerateTexChain(m_model, m_texsurfaces, m_pWorldModel.get(), m_pLeaf.get(), TEXCHAIN_PASS_SOLID_WITH_SKY, m_vDrawAttribBuffer);
		}

		if (m_pLeaf->m_bIsClosing.load())
			return false;

		m_IsDataReady.store(true);
		return true;
	}

	void UploadResource() override
	{
		if (m_pLeaf->m_bIsClosing.load())
			return;

		if (m_vDrawAttribBuffer.size() > 0)
		{
			m_pLeaf->hABO = GL_GenBuffer();

			GL_UploadDataToABOStaticDraw(m_pLeaf->hABO, sizeof(CDrawIndexAttrib) * m_vDrawAttribBuffer.size(), m_vDrawAttribBuffer.data());
		}

		for (auto surf : m_watersurfaces)
		{
			R_GetWaterSurfaceModel(m_model, surf, 0, m_pWorldModel.get(), m_pLeaf.get());
		}

		for (auto surf : m_reversedwatersurfaces)
		{
			R_GetWaterSurfaceModel(m_model, surf, 1, m_pWorldModel.get(), m_pLeaf.get());
		}

		R_GenerateResourceForWaterModels(m_model, m_pWorldModel.get(), m_pLeaf.get());
	}
};

void R_GenerateWorldSurfaceModelLeaf(const std::shared_ptr<CWorldSurfaceModel>& pModel,	model_t* mod, mleaf_t* leaf, bool bIsWorldLeaf)
{
	int leafIndex = (bIsWorldLeaf && leaf) ? R_GetWorldLeafIndex(mod, leaf) : 0;

	if (pModel->GetLeafByIndex(leafIndex))
		return;

	auto pWorldModel = pModel->m_pWorldModel.lock();

	auto pLeaf = std::make_shared<CWorldSurfaceLeaf>(leaf);

	pLeaf->m_pModel = pModel;

	if (pModel->m_vLeaves.size() < leafIndex + 1)
	{
		pModel->m_vLeaves.resize(leafIndex + 1);
	}

	pModel->m_vLeaves[leafIndex] = pLeaf;

	pLeaf->m_pGameResourceAsyncLoadTask = std::make_shared<CWorldSurfaceLeafAsyncLoadTask>(pLeaf, pWorldModel, mod, leaf, bIsWorldLeaf);

	pLeaf->m_pGameResourceAsyncLoadTask->StartAsyncTask();
}

void R_LinkShadowProxyForWorldSurfaceModel(CWorldSurfaceModel* pModel)
{
	std::shared_ptr<CWorldSurfaceShadowProxyModel> pShadowProxyModel;

	pModel->m_pShadowProxyModel.reset();
	pModel->m_pShadowProxyDraws.clear();

	auto mod = pModel->m_model;

	auto worldmodel = R_FindWorldModelByModel(mod);

	auto it = g_WorldSurfaceShadowProxyModels.find(mod->name);

	if (it != g_WorldSurfaceShadowProxyModels.end())
	{
		pShadowProxyModel = it->second;
	}
	else
	{
		
		it = g_WorldSurfaceShadowProxyModels.find(worldmodel->name);

		if (it != g_WorldSurfaceShadowProxyModels.end())
		{
			pShadowProxyModel = it->second;
		}
	}

	if (pShadowProxyModel)
	{
		pModel->m_pShadowProxyModel = pShadowProxyModel;

		if (worldmodel == mod)
		{
			for (const auto &pDraw : pShadowProxyModel->DrawList)
			{
				if (pDraw->name.compare(mod->name) == 0 || pDraw->name.starts_with("M_0_"))
				{
					pModel->m_pShadowProxyDraws.emplace_back(pDraw);
				}
			}
		}
		else
		{
			if (mod->name[0] == '*')
			{
				int submodelIndex = atoi(mod->name + 1);

				if (submodelIndex >= 0 && submodelIndex < worldmodel->numsubmodels)
				{
					char prefix[32]{ };
					snprintf(prefix, sizeof(prefix), "M_%d_", submodelIndex);

					for (const auto& pDraw : pShadowProxyModel->DrawList)
					{
						if (pDraw->name.compare(mod->name) == 0 || pDraw->name.starts_with(prefix))
						{
							pModel->m_pShadowProxyDraws.emplace_back(pDraw);
						}
					}
				}
			}
		}
	}
}

std::shared_ptr<CWorldSurfaceModel> R_GenerateWorldSurfaceModel(model_t* mod)
{
	auto pModel = std::make_shared<CWorldSurfaceModel>(mod);

	R_LinkShadowProxyForWorldSurfaceModel(pModel.get());
	
	if (mod == (*cl_worldmodel))
	{
		auto pWorldModel = R_GetWorldSurfaceWorldModel(mod);

		pModel->m_pWorldModel = pWorldModel;

		if ((int)r_leaf_lazy_load->value == 0)
		{
			std::set<mleaf_t*> vPossibleLeafs;

			R_RecursiveFindLeaves(mod->nodes, vPossibleLeafs);

			pModel->m_vLeaves.resize(vPossibleLeafs.size());

			for (auto leaf : vPossibleLeafs)
			{
				R_GenerateWorldSurfaceModelLeaf(pModel, mod, leaf, true);
			}
		}
	}
	else
	{
		auto worldmodel = R_FindWorldModelByModel(mod);

		auto pWorldModel = R_GetWorldSurfaceWorldModel(worldmodel);

		pModel->m_pWorldModel = pWorldModel;

		pModel->m_vLeaves.resize(1);

		if ((int)r_leaf_lazy_load->value == 0)
		{
			R_GenerateWorldSurfaceModelLeaf(pModel, mod, nullptr, false);
		}
	}

	return pModel;
}

std::shared_ptr<CWorldSurfaceModel> R_GetWorldSurfaceModel(model_t* mod)
{
	if (mod->type != mod_brush)
	{
		Sys_Error("R_GetWorldSurfaceModel: invalid mod type!");
		return nullptr;
	}

	auto modelindex = EngineGetModelIndex(mod);

	if (modelindex == -1)
		return NULL;

	if (modelindex >= (int)g_WorldSurfaceModels.size())
	{
		g_WorldSurfaceModels.resize(modelindex + 1);
	}

	auto pModel = g_WorldSurfaceModels[modelindex];

	if (!pModel)
	{
		pModel = R_GenerateWorldSurfaceModel(mod);

		g_WorldSurfaceModels[modelindex] = pModel;
	}

	return pModel;
}

int R_FindTextureIdByTexture(model_t* mod, texture_t* ptex)
{
	for (int i = 0; i < mod->numtextures; ++i)
	{
		if (mod->textures[i] == ptex)
			return i;
	}

	return -1;
}

msurface_t* R_GetWorldSurfaceByIndex(model_t* mod, int index)
{
	msurface_t* surf;

	if (g_iEngineType == ENGINE_GOLDSRC_HL25)
	{
		surf = (((msurface_hl25_t*)mod->surfaces) + index);
	}
	else
	{
		surf = mod->surfaces + index;
	}

	return surf;
}

int R_GetWorldSurfaceIndex(model_t* mod, msurface_t* surf)
{
	if (g_iEngineType == ENGINE_GOLDSRC_HL25)
	{
		auto surf25 = (msurface_hl25_t*)surf;
		auto surfbase = (msurface_hl25_t*)mod->surfaces;
		auto surfend = surfbase + mod->numsurfaces;

		if (surf25 >= surfbase && surf25 < surfend)
			return surf25 - surfbase;

		return -1;
	}

	auto surfbase = mod->surfaces;
	auto surfend = surfbase + mod->numsurfaces;

	if (surf >= surfbase && surf < surfend)
		return surf - surfbase;

	return -1;
}

mleaf_t* R_GetWorldLeafByIndex(model_t* mod, int index)
{
	return mod->leafs + index;
}

int R_GetWorldLeafIndex(model_t* mod, mleaf_t* leaf)
{
	return leaf - mod->leafs;
}

model_t* R_FindWorldModelByModel(model_t* m)
{
	for (auto mod : g_WorldSurfaceRenderer.vWorldModels)
	{
		if (mod->vertexes == m->vertexes)
			return mod;
	}
	return nullptr;
}

model_t* R_FindWorldModelBySurface(msurface_t* psurf)
{
	for (auto mod : g_WorldSurfaceRenderer.vWorldModels)
	{
		if (g_iEngineType == ENGINE_GOLDSRC_HL25)
		{
			auto psurf_hl25 = (msurface_hl25_t*)psurf;
			auto pbase_hl25 = (msurface_hl25_t*)mod->surfaces;
			auto pend_hl25 = (msurface_hl25_t*)R_GetWorldSurfaceByIndex(mod, mod->numsurfaces);

			if (psurf_hl25 >= pbase_hl25 && psurf_hl25 < pend_hl25)
			{
				return mod;
			}
		}
		else
		{
			auto pbase = mod->surfaces;
			auto pend = R_GetWorldSurfaceByIndex(mod, mod->numsurfaces);

			if (psurf >= pbase && psurf < pend)
			{
				return mod;
			}
		}
	}

	return nullptr;
}

model_t* R_FindWorldModelByNode(mnode_t* pnode)
{
	for (auto mod : g_WorldSurfaceRenderer.vWorldModels)
	{
		auto pbase = mod->nodes;
		auto pend = pbase + mod->numnodes;

		if (pnode >= pbase && pnode < pend)
		{
			return mod;
		}
	}

	return nullptr;
}

void R_PolygonToTriangleList(const std::vector<vertex3f_t>& vPolyVertices, std::vector<uint32_t>& vOutIndiceBuffer)
{
    // 严格遵循 AdvancedMath.hpp 中 Polygon3DTriangulator 的流程：
    // 1) 计算平均法线并确定投影平面
    // 2) 将 3D 多边形投影到 2D 平面
    // 3) 使用 2D 耳切三角化（与 Polygon2DTriangulator 一致）

    const size_t VertCount = vPolyVertices.size();
    if (VertCount <= 2)
        return;
    if (VertCount == 3)
    {
        vOutIndiceBuffer.push_back(0);
        vOutIndiceBuffer.push_back(1);
        vOutIndiceBuffer.push_back(2);
        return;
    }

    // 计算平均法线（与 Polygon3DTriangulator 相同的方式：累积对齐后的逐顶点法线）
    float Normal[3] = {0, 0, 0};
    auto addScaled = [](float out[3], const float in[3], float s) {
        out[0] += in[0] * s;
        out[1] += in[1] * s;
        out[2] += in[2] * s;
    };

    for (size_t i = 0; i < VertCount; ++i)
    {
        const vec3_t& V0 = vPolyVertices[(i + 0) % VertCount].v;
        const vec3_t& V1 = vPolyVertices[(i + 1) % VertCount].v;
        const vec3_t& V2 = vPolyVertices[(i + 2) % VertCount].v;
        vec3_t V0_V1 = { V1[0] - V0[0], V1[1] - V0[1], V1[2] - V0[2] };
        vec3_t V1_V2 = { V2[0] - V1[0], V2[1] - V1[1], V2[2] - V1[2] };
        vec3_t VertexNormal{};
        CrossProduct(V0_V1, V1_V2, VertexNormal);
        float sign = (Normal[0]*VertexNormal[0] + Normal[1]*VertexNormal[1] + Normal[2]*VertexNormal[2]) >= 0.0f ? 1.0f : -1.0f;
        addScaled(Normal, VertexNormal, sign);
    }

    if (Normal[0] == 0.0f && Normal[1] == 0.0f && Normal[2] == 0.0f)
    {
        // 与 Polygon3DTriangulator 一致：点共线，返回空结果
        return;
    }

    // 选择切线向量（按最大分量规则），并归一化 Tangent/Bitangent
    float AbsNormal[3] = { fabsf(Normal[0]), fabsf(Normal[1]), fabsf(Normal[2]) };
    vec3_t Tangent{};
    if (AbsNormal[2] > (AbsNormal[0] > AbsNormal[1] ? AbsNormal[0] : AbsNormal[1]))
    {
        // cross({0,1,0}, Normal)
        vec3_t Y = {0,1,0};
        CrossProduct(Y, Normal, Tangent);
    }
    else if (AbsNormal[1] > (AbsNormal[0] > AbsNormal[2] ? AbsNormal[0] : AbsNormal[2]))
    {
        // cross({1,0,0}, Normal)
        vec3_t X = {1,0,0};
        CrossProduct(X, Normal, Tangent);
    }
    else
    {
        // cross({0,0,1}, Normal)
        vec3_t Z = {0,0,1};
        CrossProduct(Z, Normal, Tangent);
    }

    if (VectorLength(Tangent) == 0.0f)
        return;
    VectorNormalize(Tangent);

    vec3_t Bitangent{};
    CrossProduct(Normal, Tangent, Bitangent);
    if (VectorLength(Bitangent) == 0.0f)
        return;
    VectorNormalize(Bitangent);

    auto dot3 = [](const vec3_t& a, const vec3_t& b) -> float {
        return a[0]*b[0] + a[1]*b[1] + a[2]*b[2];
    };

    struct Vec2 { float x, y; };
    std::vector<Vec2> Poly2D;
    Poly2D.reserve(VertCount);
    for (size_t i = 0; i < VertCount; ++i)
    {
        const vec3_t& P = vPolyVertices[i].v;
        Poly2D.push_back({ dot3(Tangent, P), dot3(Bitangent, P) });
    }

    // 以下为 2D 耳切三角化流程（等价于 Polygon2DTriangulator::Triangulate）：
    const int N = static_cast<int>(Poly2D.size());
    const int TriangleCount = N - 2;
    if (TriangleCount == 1)
    {
        vOutIndiceBuffer.push_back(0);
        vOutIndiceBuffer.push_back(1);
        vOutIndiceBuffer.push_back(2);
        return;
    }

    // 找到最左顶点以确定多边形绕序
    int LeftmostVertIdx = 0;
    for (int i = 1; i < N; ++i)
    {
        if (Poly2D[i].x < Poly2D[LeftmostVertIdx].x)
            LeftmostVertIdx = i;
    }

    auto WrapIndex = [](int idx, int Count) {
        return ((idx % Count) + Count) % Count;
    };

    auto GetWinding = [](const Vec2& V0, const Vec2& V1, const Vec2& V2) -> float {
        return (V1.x - V0.x) * (V2.y - V1.y) - (V2.x - V1.x) * (V1.y - V0.y);
    };

    float PolygonWinding = 0.0f;
    for (int i = 0; i < N && PolygonWinding == 0.0f; ++i)
    {
        const Vec2& V0 = Poly2D[WrapIndex(LeftmostVertIdx + i - 1, N)];
        const Vec2& V1 = Poly2D[WrapIndex(LeftmostVertIdx + i + 0, N)];
        const Vec2& V2 = Poly2D[WrapIndex(LeftmostVertIdx + i + 1, N)];
        PolygonWinding = GetWinding(V0, V1, V2);
    }
    if (PolygonWinding == 0.0f)
    {
        // 顶点共线
        return;
    }
    PolygonWinding = PolygonWinding > 0.0f ? 1.0f : -1.0f;

    enum VertexType { Convexx = 0, Reflex = 1, Ear = 2 };
    std::vector<int> RemainingIds(N);
    std::vector<VertexType> VertTypes(N, Convexx);
    for (int i = 0; i < N; ++i)
        RemainingIds[i] = i;

    auto CheckConvex = [&](int vert_id) -> VertexType {
        const int R = static_cast<int>(RemainingIds.size());
        const int Idx0 = RemainingIds[WrapIndex(vert_id - 1, R)];
        const int Idx1 = RemainingIds[WrapIndex(vert_id + 0, R)];
        const int Idx2 = RemainingIds[WrapIndex(vert_id + 1, R)];
        const auto& V0 = Poly2D[Idx0];
        const auto& V1 = Poly2D[Idx1];
        const auto& V2 = Poly2D[Idx2];
        return GetWinding(V0, V1, V2) * PolygonWinding < 0.0f ? Reflex : Convexx;
    };

    auto IsPointInsideTriangle2D = [](const Vec2& V0, const Vec2& V1, const Vec2& V2, const Vec2& P) -> bool {
        // AllowEdges = false（严格与 AdvancedMath 行为一致）
        auto crossZ = [](const Vec2& A, const Vec2& B) { return A.x * B.y - A.y * B.x; };
        Vec2 E0{V1.x - V0.x, V1.y - V0.y};
        Vec2 E1{V2.x - V1.x, V2.y - V1.y};
        Vec2 E2{V0.x - V2.x, V0.y - V2.y};
        Vec2 P0{P.x - V0.x, P.y - V0.y};
        Vec2 P1{P.x - V1.x, P.y - V1.y};
        Vec2 P2{P.x - V2.x, P.y - V2.y};
        float z0 = crossZ(E0, P0);
        float z1 = crossZ(E1, P1);
        float z2 = crossZ(E2, P2);
        return (z0 > 0 && z1 > 0 && z2 > 0) || (z0 < 0 && z1 < 0 && z2 < 0);
    };

    auto CheckEar = [&](int vert_id) -> VertexType {
        const int R = static_cast<int>(RemainingIds.size());
        const int Idx0 = RemainingIds[WrapIndex(vert_id - 1, R)];
        const int Idx1 = RemainingIds[WrapIndex(vert_id + 0, R)];
        const int Idx2 = RemainingIds[WrapIndex(vert_id + 1, R)];
        const auto& V0 = Poly2D[Idx0];
        const auto& V1 = Poly2D[Idx1];
        const auto& V2 = Poly2D[Idx2];

        // 检查所有剩余点是否在三角形内部（不含边）
        for (int k : RemainingIds)
        {
            if (k == Idx0 || k == Idx1 || k == Idx2)
                continue;
            // 只对非凸/非耳的点做严格测试（与 AdvancedMath 开发版逻辑一致的近似实现）
            if (VertTypes[k] == Convexx || VertTypes[k] == Ear)
                continue;
            if (IsPointInsideTriangle2D(V0, V1, V2, Poly2D[k]))
                return Convexx; // 非耳
        }
        return Ear;
    };

    // 初始标记凸/凹
    for (int vid = 0; vid < N; ++vid)
        VertTypes[vid] = CheckConvex(vid);
    // 标记耳朵
    for (int vid = 0; vid < N; ++vid)
        if (VertTypes[vid] == Convexx)
            VertTypes[vid] = CheckEar(vid);

    // 开始剪耳
    while (RemainingIds.size() > 3)
    {
        int R = static_cast<int>(RemainingIds.size());
        int ear_vert_id = 0;
        for (; ear_vert_id < R; ++ear_vert_id)
        {
            const int Idx = RemainingIds[ear_vert_id];
            if (VertTypes[Idx] == Ear)
                break;
        }
        if (ear_vert_id == R)
        {
            // 与 AdvancedMath 一致：没有耳朵则强制选择 0 位置
            ear_vert_id = 0;
        }

        const int Idx0 = RemainingIds[WrapIndex(ear_vert_id - 1, R)];
        const int Idx1 = RemainingIds[ear_vert_id];
        const int Idx2 = RemainingIds[WrapIndex(ear_vert_id + 1, R)];
        vOutIndiceBuffer.emplace_back(static_cast<uint32_t>(Idx0));
        vOutIndiceBuffer.emplace_back(static_cast<uint32_t>(Idx1));
        vOutIndiceBuffer.emplace_back(static_cast<uint32_t>(Idx2));

        RemainingIds.erase(RemainingIds.begin() + ear_vert_id);
        --R;
        if (R > 3)
        {
            const int IdxL = RemainingIds[WrapIndex(ear_vert_id - 1, R)];
            const int IdxR = RemainingIds[WrapIndex(ear_vert_id + 0, R)];
            VertTypes[IdxL] = CheckConvex(ear_vert_id - 1);
            VertTypes[IdxR] = CheckConvex(ear_vert_id + 0);
            if (VertTypes[IdxL] == Convexx)
                VertTypes[IdxL] = CheckEar(ear_vert_id - 1);
            if (VertTypes[IdxR] == Convexx)
                VertTypes[IdxR] = CheckEar(ear_vert_id + 0);
        }
    }

    // 输出最后一个三角形
    if (RemainingIds.size() == 3)
    {
        vOutIndiceBuffer.emplace_back(static_cast<uint32_t>(RemainingIds[0]));
        vOutIndiceBuffer.emplace_back(static_cast<uint32_t>(RemainingIds[1]));
        vOutIndiceBuffer.emplace_back(static_cast<uint32_t>(RemainingIds[2]));
    }
}

uint32_t R_FindWorldMaterialId(int gl_texturenum)
{
	for (size_t i = 0; i < g_WorldSurfaceRenderer.vWorldMaterialTextureMapping.size(); ++i)
	{
		if (g_WorldSurfaceRenderer.vWorldMaterialTextureMapping[i] == gl_texturenum)
			return (uint32_t)i;
	}
	return (uint32_t)-1;
}

void R_GenerateWorldMaterialForWorldModel(model_t* mod)
{
	for (int i = 0; i < mod->numtextures + 1; ++i)
	{
		texture_t* t = nullptr;

		if (i == mod->numtextures)
			t = R_GetEmptyWorldTexture();
		else
			t = mod->textures[i];

		if (!t)
			continue;

		if (R_FindWorldMaterialId(t->gl_texturenum) != -1)
			continue;

		world_material_t mat;

		mat.diffuseScale[0] = 1;
		mat.diffuseScale[1] = 1;
		mat.detailScale[0] = 1;
		mat.detailScale[1] = 1;
		mat.normalScale[0] = 1;
		mat.normalScale[1] = 1;
		mat.parallaxScale[0] = 1;
		mat.parallaxScale[1] = 1;
		mat.specularScale[0] = 1;
		mat.specularScale[1] = 1;

		auto pRenderMaterial = R_GetRenderMaterialForWorldTexture(t);

		if (pRenderMaterial)
		{
			if (pRenderMaterial->textures[WSURF_DIFFUSE_TEXTURE].gltexturenum)
			{
				mat.diffuseScale[0] = pRenderMaterial->textures[WSURF_DIFFUSE_TEXTURE].scaleX;
				mat.diffuseScale[1] = pRenderMaterial->textures[WSURF_DIFFUSE_TEXTURE].scaleY;
			}
			if (pRenderMaterial->textures[WSURF_DETAIL_TEXTURE].gltexturenum)
			{
				mat.detailScale[0] = pRenderMaterial->textures[WSURF_DETAIL_TEXTURE].scaleX;
				mat.detailScale[1] = pRenderMaterial->textures[WSURF_DETAIL_TEXTURE].scaleY;
			}
			if (pRenderMaterial->textures[WSURF_NORMAL_TEXTURE].gltexturenum)
			{
				mat.normalScale[0] = pRenderMaterial->textures[WSURF_NORMAL_TEXTURE].scaleX;
				mat.normalScale[1] = pRenderMaterial->textures[WSURF_NORMAL_TEXTURE].scaleY;
			}
			if (pRenderMaterial->textures[WSURF_PARALLAX_TEXTURE].gltexturenum)
			{
				mat.parallaxScale[0] = pRenderMaterial->textures[WSURF_PARALLAX_TEXTURE].scaleX;
				mat.parallaxScale[1] = pRenderMaterial->textures[WSURF_PARALLAX_TEXTURE].scaleY;
			}
			if (pRenderMaterial->textures[WSURF_SPECULAR_TEXTURE].gltexturenum)
			{
				mat.specularScale[0] = pRenderMaterial->textures[WSURF_SPECULAR_TEXTURE].scaleX;
				mat.specularScale[1] = pRenderMaterial->textures[WSURF_SPECULAR_TEXTURE].scaleY;
			}
		}

		g_WorldSurfaceRenderer.vWorldMaterialTextureMapping.emplace_back(t->gl_texturenum);
		g_WorldSurfaceRenderer.vWorldMaterials.emplace_back(mat);
	}
}

std::shared_ptr<CWorldSurfaceWorldModel> R_GenerateWorldSurfaceWorldModel(model_t* mod)
{
	std::vector<brushvertex_t> vVertexDataBuffer;
	std::vector<brushvertextbn_t> vVertexTBNDataBuffer;
	std::vector<brushinstancedata_t> vInstanceDataBuffer;
	std::vector<uint32_t> vIndiceBuffer;

	vVertexDataBuffer.reserve(mod->numvertexes);
	vInstanceDataBuffer.reserve(mod->numsurfaces);
	vIndiceBuffer.reserve(mod->numvertexes * 4);

	auto pWorldModel = std::make_shared<CWorldSurfaceWorldModel>(mod);

	pWorldModel->m_vFaceBuffer.resize(mod->numsurfaces);

	for (int i = 0; i < mod->numsurfaces; i++)
	{
		auto surf = R_GetWorldSurfaceByIndex(mod, i);
		auto poly = surf->polys;
		auto pBrushFace = &pWorldModel->m_vFaceBuffer[i];

		VectorCopy(surf->texinfo->vecs[0], pBrushFace->s_tangent);
		VectorCopy(surf->texinfo->vecs[1], pBrushFace->t_tangent);
		VectorNormalize(pBrushFace->s_tangent);
		VectorNormalize(pBrushFace->t_tangent);
		VectorCopy(surf->plane->normal, pBrushFace->normal);
		pBrushFace->index = i;
		pBrushFace->flags = surf->flags;

		if (surf->flags & SURF_PLANEBACK)
		{
			VectorInverse(pBrushFace->normal);
			VectorInverse(pBrushFace->s_tangent);
			VectorInverse(pBrushFace->t_tangent);
		}

		if (surf->lightmaptexturenum + 1 > g_WorldSurfaceRenderer.iNumLightmapTextures)
			g_WorldSurfaceRenderer.iNumLightmapTextures = surf->lightmaptexturenum + 1;

		auto ptexture = (surf->texinfo && surf->texinfo->texture) ? surf->texinfo->texture : R_GetEmptyWorldTexture();

		if (pBrushFace->flags & SURF_DRAWTURB)
		{
			if (1)
			{
				uint32_t nBrushStartIndex = (uint32_t)vIndiceBuffer.size();

				for (poly = surf->polys; poly; poly = poly->next)
				{
					uint32_t nPolyStartIndex = (uint32_t)vVertexDataBuffer.size();

					std::vector<vertex3f_t> vPolyVertices;

					float* v = poly->verts[0];

					for (int j = 0; j < poly->numverts; j++, v += VERTEXSIZE)
					{
						vertex3f_t tempVertex;
						VectorCopy(v, tempVertex.v);

						brushvertex_t tempVertexData;
						VectorCopy(v, tempVertexData.pos);
						tempVertexData.texcoord[0] = v[3];
						tempVertexData.texcoord[1] = v[4];
						tempVertexData.lightmaptexcoord[0] = v[5];
						tempVertexData.lightmaptexcoord[1] = v[6];

						brushvertextbn_t tempVertexTBNData;
						VectorCopy(pBrushFace->normal, tempVertexTBNData.normal);
						VectorCopy(pBrushFace->s_tangent, tempVertexTBNData.s_tangent);
						VectorCopy(pBrushFace->t_tangent, tempVertexTBNData.t_tangent);

						vPolyVertices.emplace_back(tempVertex);
						vVertexDataBuffer.emplace_back(tempVertexData);
						vVertexTBNDataBuffer.emplace_back(tempVertexTBNData);
					}

					std::vector<uint32_t> vTriangleListIndices;
					R_PolygonToTriangleList(vPolyVertices, vTriangleListIndices);

					for (size_t k = 0; k < vTriangleListIndices.size(); ++k)
					{
						vIndiceBuffer.emplace_back(nPolyStartIndex + vTriangleListIndices[k]);
					}

					pBrushFace->poly_count++;
				}
				uint32_t nBrushCurrentIndex = (uint32_t)vIndiceBuffer.size();
				pBrushFace->start_index = nBrushStartIndex;
				pBrushFace->index_count = nBrushCurrentIndex - nBrushStartIndex;
			}
			if (1)
			{
				uint32_t nBrushStartIndex = (uint32_t)vIndiceBuffer.size();

				for (poly = surf->polys; poly; poly = poly->next)
				{
					uint32_t nPolyStartIndex = (uint32_t)vVertexDataBuffer.size();

					std::vector<vertex3f_t> vPolyVertices;

					float* v = poly->verts[0];

					for (int j = 0; j < poly->numverts; j++, v += VERTEXSIZE)
					{
						vertex3f_t tempVertex;
						VectorCopy(v, tempVertex.v);

						brushvertex_t tempVertexData;
						VectorCopy(v, tempVertexData.pos);
						tempVertexData.texcoord[0] = v[3];
						tempVertexData.texcoord[1] = v[4];
						tempVertexData.lightmaptexcoord[0] = v[5];
						tempVertexData.lightmaptexcoord[1] = v[6];

						brushvertextbn_t tempVertexTBNData;
						VectorCopy(pBrushFace->normal, tempVertexTBNData.normal);
						VectorCopy(pBrushFace->s_tangent, tempVertexTBNData.s_tangent);
						VectorCopy(pBrushFace->t_tangent, tempVertexTBNData.t_tangent);

						vPolyVertices.emplace_back(tempVertex);
						vVertexDataBuffer.emplace_back(tempVertexData);
						vVertexTBNDataBuffer.emplace_back(tempVertexTBNData);
					}

					std::vector<uint32_t> vTriangleListIndices;
					R_PolygonToTriangleList(vPolyVertices, vTriangleListIndices);

					for (size_t k = 0; k < vTriangleListIndices.size(); ++k)
					{
						vIndiceBuffer.emplace_back(nPolyStartIndex + vTriangleListIndices[vTriangleListIndices.size() - 1 - k]);
					}

					pBrushFace->poly_count++;
				}
				uint32_t nBrushCurrentIndex = (uint32_t)vIndiceBuffer.size();
				pBrushFace->reverse_start_index = nBrushStartIndex;
				pBrushFace->reverse_index_count = nBrushCurrentIndex - nBrushStartIndex;
			}
		}
		else
		{
			uint32_t nBrushStartIndex = (uint32_t)vIndiceBuffer.size();

			for (poly = surf->polys; poly; poly = poly->next)
			{
				uint32_t nPolyStartIndex = (uint32_t)vVertexDataBuffer.size();

				std::vector<vertex3f_t> vPolyVertices;

				float* v = poly->verts[0];

				vertex3f_t tempVertex[3];
				brushvertex_t tempVertexData[3];
				brushvertextbn_t tempVertexTBNData[3];

				for (int j = 0; j < 3; j++, v += VERTEXSIZE)
				{
					VectorCopy(v, tempVertex[j].v);

					VectorCopy(v, tempVertexData[j].pos);

					tempVertexData[j].texcoord[0] = v[3];
					tempVertexData[j].texcoord[1] = v[4];

					tempVertexData[j].lightmaptexcoord[0] = v[5];
					tempVertexData[j].lightmaptexcoord[1] = v[6];

					VectorCopy(pBrushFace->normal, tempVertexTBNData[j].normal);
					VectorCopy(pBrushFace->s_tangent, tempVertexTBNData[j].s_tangent);
					VectorCopy(pBrushFace->t_tangent, tempVertexTBNData[j].t_tangent);
				}

				vPolyVertices.emplace_back(tempVertex[0]);
				vPolyVertices.emplace_back(tempVertex[1]);
				vPolyVertices.emplace_back(tempVertex[2]);
				vVertexDataBuffer.emplace_back(tempVertexData[0]);
				vVertexDataBuffer.emplace_back(tempVertexData[1]);
				vVertexDataBuffer.emplace_back(tempVertexData[2]);
				vVertexTBNDataBuffer.emplace_back(tempVertexTBNData[0]);
				vVertexTBNDataBuffer.emplace_back(tempVertexTBNData[1]);
				vVertexTBNDataBuffer.emplace_back(tempVertexTBNData[2]);

				for (int j = 0; j < (poly->numverts - 3); j++, v += VERTEXSIZE)
				{
					memcpy(&tempVertex[1], &tempVertex[2], sizeof(vertex3f_t));
					memcpy(&tempVertexData[1], &tempVertexData[2], sizeof(brushvertex_t));

					VectorCopy(v, tempVertex[2].v);

					VectorCopy(v, tempVertexData[2].pos);

					tempVertexData[2].texcoord[0] = v[3];
					tempVertexData[2].texcoord[1] = v[4];
					tempVertexData[2].lightmaptexcoord[0] = v[5];
					tempVertexData[2].lightmaptexcoord[1] = v[6];

					VectorCopy(pBrushFace->normal, tempVertexTBNData[2].normal);
					VectorCopy(pBrushFace->s_tangent, tempVertexTBNData[2].s_tangent);
					VectorCopy(pBrushFace->t_tangent, tempVertexTBNData[2].t_tangent);

					vPolyVertices.emplace_back(tempVertex[0]);
					vPolyVertices.emplace_back(tempVertex[1]);
					vPolyVertices.emplace_back(tempVertex[2]);
					vVertexDataBuffer.emplace_back(tempVertexData[0]);
					vVertexDataBuffer.emplace_back(tempVertexData[1]);
					vVertexDataBuffer.emplace_back(tempVertexData[2]);
					vVertexTBNDataBuffer.emplace_back(tempVertexTBNData[0]);
					vVertexTBNDataBuffer.emplace_back(tempVertexTBNData[1]);
					vVertexTBNDataBuffer.emplace_back(tempVertexTBNData[2]);
				}

				std::vector<uint32_t> vTriangleListIndices;
				R_PolygonToTriangleList(vPolyVertices, vTriangleListIndices);

				for (size_t k = 0; k < vTriangleListIndices.size(); ++k)
				{
					vIndiceBuffer.emplace_back(nPolyStartIndex + vTriangleListIndices[k]);
				}

				pBrushFace->poly_count++;
			}

			uint32_t nBrushCurrentIndex = (uint32_t)vIndiceBuffer.size();
			pBrushFace->start_index = nBrushStartIndex;
			pBrushFace->index_count = nBrushCurrentIndex - nBrushStartIndex;
		}

		pBrushFace->instance_index = (uint32_t)vInstanceDataBuffer.size();

		brushinstancedata_t tempInstanceData;

		tempInstanceData.lightmaptexturenum_texcoordscale[0] = surf->lightmaptexturenum;
		tempInstanceData.lightmaptexturenum_texcoordscale[1] = (ptexture && (pBrushFace->flags & SURF_DRAWTILED)) ? 1.0f / ptexture->width : 0;
		memcpy(&tempInstanceData.styles, surf->styles, sizeof(surf->styles));

		tempInstanceData.matId = ptexture ? R_FindWorldMaterialId(ptexture->gl_texturenum) : 0;
		vInstanceDataBuffer.emplace_back(tempInstanceData);

		pBrushFace->instance_count = 1;
	}

	pWorldModel->hEBO = GL_GenBuffer();
	GL_UploadDataToEBOStaticDraw(pWorldModel->hEBO, sizeof(uint32_t) * vIndiceBuffer.size(), vIndiceBuffer.data());

	pWorldModel->hVBO[WSURF_VBO_VERTEX] = GL_GenBuffer();
	GL_UploadDataToVBOStaticDraw(pWorldModel->hVBO[WSURF_VBO_VERTEX], sizeof(brushvertex_t) * vVertexDataBuffer.size(), vVertexDataBuffer.data());

	pWorldModel->hVBO[WSURF_VBO_VERTEXTBN] = GL_GenBuffer();
	GL_UploadDataToVBOStaticDraw(pWorldModel->hVBO[WSURF_VBO_VERTEXTBN], sizeof(brushvertextbn_t)* vVertexTBNDataBuffer.size(), vVertexTBNDataBuffer.data());

	pWorldModel->hVBO[WSURF_VBO_INSTANCE] = GL_GenBuffer();
	GL_UploadDataToVBOStaticDraw(pWorldModel->hVBO[WSURF_VBO_INSTANCE], sizeof(brushinstancedata_t) * vInstanceDataBuffer.size(), vInstanceDataBuffer.data());

	pWorldModel->hVAO = GL_GenVAO();

	GL_BindStatesForVAO(pWorldModel->hVAO, [pWorldModel]() {

		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, pWorldModel->hEBO);

		glBindBuffer(GL_ARRAY_BUFFER, pWorldModel->hVBO[WSURF_VBO_VERTEX]);

		glEnableVertexAttribArray(WSURF_VA_POSITION);
		glEnableVertexAttribArray(WSURF_VA_TEXCOORD);
		glEnableVertexAttribArray(WSURF_VA_LIGHTMAP_TEXCOORD);

		glVertexAttribPointer(WSURF_VA_POSITION, 3, GL_FLOAT, false, sizeof(brushvertex_t), OFFSET(brushvertex_t, pos));
		glVertexAttribPointer(WSURF_VA_TEXCOORD, 2, GL_FLOAT, false, sizeof(brushvertex_t), OFFSET(brushvertex_t, texcoord));
		glVertexAttribPointer(WSURF_VA_LIGHTMAP_TEXCOORD, 2, GL_FLOAT, false, sizeof(brushvertex_t), OFFSET(brushvertex_t, lightmaptexcoord));

		glBindBuffer(GL_ARRAY_BUFFER, pWorldModel->hVBO[WSURF_VBO_VERTEXTBN]);

		glEnableVertexAttribArray(WSURF_VA_NORMAL);
		glEnableVertexAttribArray(WSURF_VA_S_TANGENT);
		glEnableVertexAttribArray(WSURF_VA_T_TANGENT);

		glVertexAttribPointer(WSURF_VA_NORMAL, 3, GL_FLOAT, false, sizeof(brushvertextbn_t), OFFSET(brushvertextbn_t, normal));
		glVertexAttribPointer(WSURF_VA_S_TANGENT, 3, GL_FLOAT, false, sizeof(brushvertextbn_t), OFFSET(brushvertextbn_t, s_tangent));
		glVertexAttribPointer(WSURF_VA_T_TANGENT, 3, GL_FLOAT, false, sizeof(brushvertextbn_t), OFFSET(brushvertextbn_t, t_tangent));

		glBindBuffer(GL_ARRAY_BUFFER, pWorldModel->hVBO[WSURF_VBO_INSTANCE]);

		glEnableVertexAttribArray(WSURF_VA_TEXTURENUM);
		glEnableVertexAttribArray(WSURF_VA_STYLES);
		glEnableVertexAttribArray(WSURF_VA_MATID);

		glVertexAttribPointer(WSURF_VA_TEXTURENUM, 2, GL_FLOAT, false, sizeof(brushinstancedata_t), OFFSET(brushinstancedata_t, lightmaptexturenum_texcoordscale));
		glVertexAttribDivisor(WSURF_VA_TEXTURENUM, 1);

		glVertexAttribIPointer(WSURF_VA_STYLES, 4, GL_UNSIGNED_BYTE, sizeof(brushinstancedata_t), OFFSET(brushinstancedata_t, styles));
		glVertexAttribDivisor(WSURF_VA_STYLES, 1);
		
		glVertexAttribIPointer(WSURF_VA_MATID, 1, GL_UNSIGNED_INT, sizeof(brushinstancedata_t), OFFSET(brushinstancedata_t, matId));
		glVertexAttribDivisor(WSURF_VA_MATID, 1);
	});

	return pWorldModel;
}

std::shared_ptr<CWorldSurfaceWorldModel> R_GetWorldSurfaceWorldModel(model_t* mod)
{
	if (mod->type != mod_brush)
	{
		Sys_Error("R_GetWorldSurfaceWorldModel: invalid mod type!");
		return nullptr;
	}

	if (mod->name[0] == '*')
	{
		Sys_Error("R_GetWorldSurfaceWorldModel: invalid name \"%s\"!", mod->name);
		return nullptr;
	}

	int modelindex = EngineGetModelIndex(mod);

	if (modelindex < (int)g_WorldSurfaceWorldModels.size() && g_WorldSurfaceWorldModels[modelindex])
	{
		return g_WorldSurfaceWorldModels[modelindex];
	}

	if (modelindex >= (int)g_WorldSurfaceWorldModels.size())
	{
		g_WorldSurfaceWorldModels.resize(modelindex + 1);
	}

	R_GenerateWorldMaterialForWorldModel(mod);

	auto pWorldModel = R_GenerateWorldSurfaceWorldModel(mod);

	g_WorldSurfaceWorldModels[modelindex] = pWorldModel;

	return pWorldModel;
}

void R_GenerateSceneUBO(void)
{
	if (g_WorldSurfaceRenderer.hSceneUBO)
		return;

	g_WorldSurfaceRenderer.hSceneUBO = GL_GenBuffer();
	GL_UploadDataToUBODynamicDraw(g_WorldSurfaceRenderer.hSceneUBO, sizeof(scene_ubo_t), nullptr);
	glBindBufferBase(GL_UNIFORM_BUFFER, BINDING_POINT_SCENE_UBO, g_WorldSurfaceRenderer.hSceneUBO);

	g_WorldSurfaceRenderer.hCameraUBO = GL_GenBuffer();
	GL_UploadDataToUBODynamicDraw(g_WorldSurfaceRenderer.hCameraUBO, sizeof(camera_ubo_t) * 6, nullptr);
	glBindBufferBase(GL_UNIFORM_BUFFER, BINDING_POINT_CAMERA_UBO, g_WorldSurfaceRenderer.hCameraUBO);

	g_WorldSurfaceRenderer.hDLightUBO = GL_GenBuffer();
	GL_UploadDataToUBODynamicDraw(g_WorldSurfaceRenderer.hDLightUBO, sizeof(dlight_ubo_t), nullptr);
	glBindBufferBase(GL_UNIFORM_BUFFER, BINDING_POINT_DLIGHT_UBO, g_WorldSurfaceRenderer.hDLightUBO);

	g_WorldSurfaceRenderer.hEntityUBO = GL_GenBuffer();
	GL_UploadDataToUBODynamicDraw(g_WorldSurfaceRenderer.hEntityUBO, sizeof(entity_ubo_t), nullptr);

	g_WorldSurfaceRenderer.hDecalVBO[WSURF_VBO_VERTEX] = GL_GenBuffer();
	GL_UploadDataToVBODynamicDraw(g_WorldSurfaceRenderer.hDecalVBO[WSURF_VBO_VERTEX], sizeof(decalvertex_t) * MAX_DECALVERTS * MAX_DECALS, nullptr);

	g_WorldSurfaceRenderer.hDecalVBO[WSURF_VBO_VERTEXTBN] = GL_GenBuffer();
	GL_UploadDataToVBODynamicDraw(g_WorldSurfaceRenderer.hDecalVBO[WSURF_VBO_VERTEXTBN], sizeof(decalvertextbn_t) * MAX_DECALVERTS * MAX_DECALS, nullptr);

	g_WorldSurfaceRenderer.hDecalVBO[WSURF_VBO_INSTANCE] = GL_GenBuffer();
	GL_UploadDataToVBODynamicDraw(g_WorldSurfaceRenderer.hDecalVBO[WSURF_VBO_INSTANCE], sizeof(decalinstancedata_t) * 1 * MAX_DECALS, nullptr);

	g_WorldSurfaceRenderer.hDecalEBO = GL_GenBuffer();
	GL_UploadDataToEBODynamicDraw(g_WorldSurfaceRenderer.hDecalEBO, sizeof(uint32_t) * MAX_DECALINDICES * MAX_DECALS, nullptr);

	g_WorldSurfaceRenderer.hDecalVAO = GL_GenVAO();

	g_WorldSurfaceRenderer.hMaterialSSBO = GL_GenBuffer();
	GL_UploadDataToSSBOStaticDraw(g_WorldSurfaceRenderer.hMaterialSSBO, sizeof(world_material_t) * 1, nullptr);

	GL_BindStatesForVAO(
		g_WorldSurfaceRenderer.hDecalVAO,
		[]() {
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_WorldSurfaceRenderer.hDecalEBO);

			glBindBuffer(GL_ARRAY_BUFFER, g_WorldSurfaceRenderer.hDecalVBO[WSURF_VBO_VERTEX]);

			glEnableVertexAttribArray(WSURF_VA_POSITION);
			glEnableVertexAttribArray(WSURF_VA_TEXCOORD);
			glEnableVertexAttribArray(WSURF_VA_LIGHTMAP_TEXCOORD);

			glVertexAttribPointer(WSURF_VA_POSITION, 3, GL_FLOAT, false, sizeof(decalvertex_t), OFFSET(decalvertex_t, pos));
			glVertexAttribPointer(WSURF_VA_TEXCOORD, 2, GL_FLOAT, false, sizeof(decalvertex_t), OFFSET(decalvertex_t, texcoord));
			glVertexAttribPointer(WSURF_VA_LIGHTMAP_TEXCOORD, 2, GL_FLOAT, false, sizeof(decalvertex_t), OFFSET(decalvertex_t, lightmaptexcoord));

			glBindBuffer(GL_ARRAY_BUFFER, g_WorldSurfaceRenderer.hDecalVBO[WSURF_VBO_VERTEXTBN]);

			glEnableVertexAttribArray(WSURF_VA_NORMAL);
			glEnableVertexAttribArray(WSURF_VA_S_TANGENT);
			glEnableVertexAttribArray(WSURF_VA_T_TANGENT);

			glVertexAttribPointer(WSURF_VA_NORMAL, 3, GL_FLOAT, false, sizeof(decalvertextbn_t), OFFSET(decalvertextbn_t, normal));
			glVertexAttribPointer(WSURF_VA_S_TANGENT, 3, GL_FLOAT, false, sizeof(decalvertextbn_t), OFFSET(decalvertextbn_t, s_tangent));
			glVertexAttribPointer(WSURF_VA_T_TANGENT, 3, GL_FLOAT, false, sizeof(decalvertextbn_t), OFFSET(decalvertextbn_t, t_tangent));

			glBindBuffer(GL_ARRAY_BUFFER, g_WorldSurfaceRenderer.hDecalVBO[WSURF_VBO_INSTANCE]);

			glEnableVertexAttribArray(WSURF_VA_TEXTURENUM);
			glEnableVertexAttribArray(WSURF_VA_STYLES);
			glEnableVertexAttribArray(WSURF_VA_MATID);

			glVertexAttribPointer(WSURF_VA_TEXTURENUM, 2, GL_FLOAT, false, sizeof(decalinstancedata_t), OFFSET(decalinstancedata_t, lightmaptexturenum));
			glVertexAttribDivisor(WSURF_VA_TEXTURENUM, 1);

			glVertexAttribIPointer(WSURF_VA_STYLES, 4, GL_UNSIGNED_BYTE, sizeof(decalinstancedata_t), OFFSET(decalinstancedata_t, styles));
			glVertexAttribDivisor(WSURF_VA_STYLES, 1);

			glVertexAttribIPointer(WSURF_VA_MATID, 1, GL_UNSIGNED_INT, sizeof(brushinstancedata_t), OFFSET(brushinstancedata_t, matId));
			glVertexAttribDivisor(WSURF_VA_MATID, 1);
		}
	);

	if (g_bUseOITBlend)
	{
		size_t fragmentBufferSizeBytes = sizeof(FragmentNode) * MAX_NUM_NODES * glwidth * glheight;

		g_WorldSurfaceRenderer.hOITFragmentSSBO = GL_GenBuffer();
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, g_WorldSurfaceRenderer.hOITFragmentSSBO);
		glBufferData(GL_SHADER_STORAGE_BUFFER, fragmentBufferSizeBytes, NULL, GL_STATIC_DRAW);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, BINDING_POINT_OIT_FRAGMENT_SSBO, g_WorldSurfaceRenderer.hOITFragmentSSBO);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

		size_t numFragmentBufferSizeBytes = sizeof(uint32_t) * glwidth * glheight;

		g_WorldSurfaceRenderer.hOITNumFragmentSSBO = GL_GenBuffer();
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, g_WorldSurfaceRenderer.hOITNumFragmentSSBO);
		glBufferData(GL_SHADER_STORAGE_BUFFER, numFragmentBufferSizeBytes, NULL, GL_STATIC_DRAW);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, BINDING_POINT_OIT_NUMFRAGMENT_SSBO, g_WorldSurfaceRenderer.hOITNumFragmentSSBO);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

		g_WorldSurfaceRenderer.hOITAtomicSSBO = GL_GenBuffer();
		glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, g_WorldSurfaceRenderer.hOITAtomicSSBO);
		glBufferData(GL_ATOMIC_COUNTER_BUFFER, sizeof(uint32_t), NULL, GL_STATIC_DRAW);
		glBindBufferBase(GL_ATOMIC_COUNTER_BUFFER, BINDING_POINT_OIT_COUNTER_SSBO, g_WorldSurfaceRenderer.hOITAtomicSSBO);
		glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, 0);
	}
}

void R_ClearDetailTextureCache(void)
{
	g_WorldTextureRenderMaterials.clear();
	g_DecalTextureRenderMaterials.clear();
}

void R_ClearDecalCache(void)
{
	for (int i = 0; i < MAX_DECALS; ++i)
	{
		g_WorldSurfaceRenderer.vCachedDecals[i].indiceCount = 0;
		g_WorldSurfaceRenderer.vCachedDecals[i].instanceCount = 0;
		g_WorldSurfaceRenderer.vCachedDecals[i].pRenderMaterial = nullptr;
	}
}

void R_DrawWorldSurfaceLeafBegin(CWorldSurfaceLeaf* pLeaf)
{
	auto pModel = pLeaf->m_pModel.lock();

	auto pWorldModel = pModel->m_pWorldModel.lock();

	GL_BindVAO(pWorldModel->hVAO);
	GL_BindABO(pLeaf->hABO);
}

void R_DrawWorldSurfaceLeafEnd()
{
	GL_BindABO(0);
	GL_BindVAO(0);
}

bool R_WorldSurfaceLeafHasSky(CWorldSurfaceModel* pModel, CWorldSurfaceLeaf* pLeaf)
{
	const auto& texchain = pLeaf->TextureChainSpecial[WSURF_TEXCHAIN_SPECIAL_SKY];

	if (!texchain.drawCount)
		return false;

	return true;
}

void R_DrawWorldSurfaceModelShadowProxyInternal(CWorldSurfaceShadowProxyModel* pShadowProxyModel, CWorldSurfaceShadowProxyDraw* pShadowProxyDraw)
{
	GL_BeginDebugGroup("R_DrawWorldSurfaceModelShadowProxyInternal");

	program_state_t WSurfProgramState = 0;

	if (R_IsRenderingGBuffer())
	{
		WSurfProgramState |= WSURF_GBUFFER_ENABLED;
	}

	if (R_IsRenderingShadowView())
	{
		WSurfProgramState |= WSURF_SHADOW_CASTER_ENABLED;
	}

	if (R_IsRenderingWaterView())
	{
		WSurfProgramState |= WSURF_CLIP_WATER_ENABLED;
	}
	else if (g_bPortalClipPlaneEnabled[0])
	{
		WSurfProgramState |= WSURF_CLIP_ENABLED;
	}

	GL_BindVAO(pShadowProxyModel->hVAO);
	GL_BindABO(pShadowProxyModel->hABO);

	wsurf_program_t prog = { 0 };
	R_UseWSurfProgram(WSurfProgramState, &prog);

	glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, (void*)(pShadowProxyDraw->startOffset), pShadowProxyDraw->drawCount, 0);

	(*c_brush_polys) += pShadowProxyDraw->drawCount;

	GL_UseProgram(0);

	GL_BindABO(0);
	GL_BindVAO(0);

	GL_EndDebugGroup();
}

void R_DrawWorldSurfaceModelShadowProxy(CWorldSurfaceModel* pModel)
{
	auto pShadowProxyModel = pModel->m_pShadowProxyModel.lock();

	if (!pShadowProxyModel)
		return;

	GL_BeginDebugGroup("R_DrawWorldSurfaceModelShadowProxyInternal");

	for (size_t i = 0; i < pModel->m_pShadowProxyDraws.size(); ++i)
	{
		auto pShadowProxyDraw = pModel->m_pShadowProxyDraws[i].lock();

		if (!pShadowProxyDraw)
			return;

		if (!pShadowProxyDraw->drawCount)
			return;

		R_DrawWorldSurfaceModelShadowProxyInternal(pShadowProxyModel.get(), pShadowProxyDraw.get());
	}

	GL_EndDebugGroup();
}

void R_DrawWorldSurfaceLeafShadow(CWorldSurfaceLeaf* pLeaf, bool bWithSky)
{
	const auto& texchain = bWithSky ? pLeaf->TextureChainSpecial[WSURF_TEXCHAIN_SPECIAL_SOLID_WITH_SKY] : pLeaf->TextureChainSpecial[WSURF_TEXCHAIN_SPECIAL_SOLID];

	if (!texchain.drawCount)
		return;

	GL_BeginDebugGroup("R_DrawWorldSurfaceLeafShadow");

	program_state_t WSurfProgramState = 0;

	if (R_IsRenderingGBuffer())
	{
		WSurfProgramState |= WSURF_GBUFFER_ENABLED;
	}

	if (R_IsRenderingShadowView())
	{
		WSurfProgramState |= WSURF_SHADOW_CASTER_ENABLED;
	}

	if (R_IsRenderingWaterView())
	{
		WSurfProgramState |= WSURF_CLIP_WATER_ENABLED;
	}
	else if (g_bPortalClipPlaneEnabled[0])
	{
		WSurfProgramState |= WSURF_CLIP_ENABLED;
	}

	glDisable(GL_CULL_FACE);

	R_DrawWorldSurfaceLeafBegin(pLeaf);

	wsurf_program_t prog = { 0 };
	R_UseWSurfProgram(WSurfProgramState, &prog);

	glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, (void*)(texchain.startDrawOffset), texchain.drawCount, 0);

	(*c_brush_polys) += texchain.polyCount;

	GL_UseProgram(0);

	glEnable(GL_CULL_FACE);

	R_DrawWorldSurfaceLeafEnd();

	GL_EndDebugGroup();

}//R_DrawWorldSurfaceLeafSolid

void R_DrawWorldSurfaceLeafStatic(CWorldSurfaceModel* pModel, CWorldSurfaceLeaf* pLeaf)
{
	GL_BeginDebugGroup("R_DrawWorldSurfaceLeafStatic");

	const auto& vTexChainList = pLeaf->vTextureChainList[WSURF_TEXCHAIN_LIST_STATIC];

	for (size_t i = 0; i < vTexChainList.size(); ++i)
	{
		const auto& texchain = vTexChainList[i];

		auto base = texchain.texture;

		program_state_t WSurfProgramState = 0;

		if (g_WorldSurfaceRenderer.bDiffuseTexture)
		{
			WSurfProgramState |= WSURF_DIFFUSE_ENABLED;

			GL_BindTextureUnit(WSURF_BIND_DIFFUSE_TEXTURE, GL_TEXTURE_2D, base->gl_texturenum);

			if (texchain.pRenderMaterial)
			{
				R_BeginDetailTextureFromRenderMaterial(texchain.pRenderMaterial.get(), &WSurfProgramState);
			}
		}

		if (g_WorldSurfaceRenderer.bLightmapTexture)
		{
			if (r_draw_classify & DRAW_CLASSIFY_LIGHTMAP)
			{
				WSurfProgramState |= WSURF_LIGHTMAP_ENABLED;
			}

			if ((int)r_fullbright->value > 0 || !(*cl_worldmodel)->lightdata)
			{
				WSurfProgramState |= WSURF_FULLBRIGHT_ENABLED;
			}

			if ((*filterMode) != 0)
			{
				WSurfProgramState |= WSURF_COLOR_FILTER_ENABLED;
			}

			if (g_WorldSurfaceRenderer.iNumLegacyDLights)
			{
				WSurfProgramState |= WSURF_LEGACY_DLIGHT_ENABLED;
			}
		}
		else
		{
			WSurfProgramState |= WSURF_FULLBRIGHT_ENABLED;
		}

		if (WSurfProgramState & WSURF_LIGHTMAP_ENABLED)
		{
			if (g_WorldSurfaceRenderer.iLightmapUsedBits & (1 << 0))
			{
				WSurfProgramState |= WSURF_LIGHTMAP_INDEX_0_ENABLED;
			}
			if (g_WorldSurfaceRenderer.iLightmapUsedBits & (1 << 1))
			{
				WSurfProgramState |= WSURF_LIGHTMAP_INDEX_1_ENABLED;
			}
			if (g_WorldSurfaceRenderer.iLightmapUsedBits & (1 << 2))
			{
				WSurfProgramState |= WSURF_LIGHTMAP_INDEX_2_ENABLED;
			}
			if (g_WorldSurfaceRenderer.iLightmapUsedBits & (1 << 3))
			{
				WSurfProgramState |= WSURF_LIGHTMAP_INDEX_3_ENABLED;
			}
		}

		if (R_IsRenderingWaterView())
		{
			WSurfProgramState |= WSURF_CLIP_WATER_ENABLED;
		}

		if (g_bPortalClipPlaneEnabled[0])
		{
			WSurfProgramState |= WSURF_CLIP_ENABLED;
		}

		if (R_IsRenderingShadowView())
		{
			WSurfProgramState |= WSURF_SHADOW_CASTER_ENABLED;
		}

		if (R_IsRenderingGBuffer())
		{
			WSurfProgramState |= WSURF_GBUFFER_ENABLED;
			WSurfProgramState &= ~(WSURF_LEGACY_DLIGHT_ENABLED);
		}

		if ((*currententity)->curstate.rendermode != kRenderNormal && (*currententity)->curstate.rendermode != kRenderTransAlpha)
		{
			if ((*currententity)->curstate.rendermode == kRenderTransAdd || (*currententity)->curstate.rendermode == kRenderGlow)
				WSurfProgramState |= WSURF_ADDITIVE_BLEND_ENABLED;
			else
				WSurfProgramState |= WSURF_ALPHA_BLEND_ENABLED;
		}

		if ((*currententity)->curstate.rendermode == kRenderTransAlpha)
		{
			WSurfProgramState |= WSURF_ALPHA_SOLID_ENABLED;
		}

		if (!R_IsRenderingGBuffer())
		{
			if ((WSurfProgramState & WSURF_ADDITIVE_BLEND_ENABLED) && (int)r_fog_trans->value <= 1)
			{

			}
			else if ((WSurfProgramState & WSURF_ALPHA_BLEND_ENABLED) && (int)r_fog_trans->value <= 0)
			{

			}
			else
			{
				if (R_IsRenderingFog())
				{
					if (r_fog_mode == GL_LINEAR)
					{
						WSurfProgramState |= WSURF_LINEAR_FOG_ENABLED;
					}
					else if (r_fog_mode == GL_EXP)
					{
						WSurfProgramState |= WSURF_EXP_FOG_ENABLED;
					}
					else if (r_fog_mode == GL_EXP2)
					{
						WSurfProgramState |= WSURF_EXP2_FOG_ENABLED;
					}

					if (!R_IsRenderingGammaBlending() && r_linear_fog_shift->value > 0)
					{
						WSurfProgramState |= WSURF_LINEAR_FOG_SHIFT_ENABLED;
					}
				}
			}
		}

		if (R_IsRenderingGammaBlending())
		{
			WSurfProgramState |= WSURF_GAMMA_BLEND_ENABLED;
		}

		if (r_draw_oitblend && (WSurfProgramState & (WSURF_ALPHA_BLEND_ENABLED | WSURF_ADDITIVE_BLEND_ENABLED)))
		{
			WSurfProgramState |= WSURF_OIT_BLEND_ENABLED;
		}

		R_DrawWorldSurfaceLeafBegin(pLeaf);

		GL_BeginStencilWrite(STENCIL_MASK_HAS_DECAL, STENCIL_MASK_HAS_DECAL);

		wsurf_program_t prog = { 0 };
		R_UseWSurfProgram(WSurfProgramState, &prog);

		glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, (void*)(texchain.startDrawOffset), texchain.drawCount, 0);

		(*c_brush_polys) += texchain.polyCount;

		R_EndDetailTexture(WSurfProgramState);

		GL_UseProgram(0);

		R_DrawWorldSurfaceLeafEnd();
	}

	GL_EndDebugGroup();

}//R_DrawWorldSurfaceLeafStatic

texture_t* R_GetAnimatedTexture(texture_t* base)
{
	if (g_iEngineType == ENGINE_SVENGINE)
	{
		if ((*currententity)->curstate.effects & EF_FRAMEANIMTEXTURES)
		{
			if ((*currententity)->curstate.frame > 0)
			{
				int frame_count = 0;
				int total_frame = (*currententity)->curstate.frame;
				do
				{
					if (base->anim_next)
						base = base->anim_next;
					++frame_count;
				} while (frame_count < total_frame);
			}
		}
		else
		{
			if ((*currententity)->curstate.frame && base->alternate_anims)
				base = base->alternate_anims;

			if (!((*currententity)->curstate.effects & EF_NOANIMTEXTURES))
			{
				int reletive = (int)((*cl_time) * 10.0f) % base->anim_total;

				int loop_count = 0;

				while (base->anim_min > reletive || base->anim_max <= reletive)
				{
					base = base->anim_next;

					if (!base)
					{
						Sys_Error("R_TextureAnimation: broken cycle");
					}
					if (++loop_count > 100)
					{
						Sys_Error("R_TextureAnimation: infinite cycle");
					}
				}
			}
		}
	}
	else
	{
		if ((*currententity)->curstate.frame && base->alternate_anims)
			base = base->alternate_anims;

		int reletive = (int)((*cl_time) * 10.0f) % base->anim_total;

		int loop_count = 0;

		while (base->anim_min > reletive || base->anim_max <= reletive)
		{
			base = base->anim_next;

			if (!base)
			{
				Sys_Error("R_TextureAnimation: broken cycle");
			}
			if (++loop_count > 100)
			{
				Sys_Error("R_TextureAnimation: infinite cycle");
			}
		}
	}
	return base;
}

void R_DrawWorldSurfaceLeafAnim(CWorldSurfaceModel* pModel, CWorldSurfaceLeaf* pLeaf)
{
	GL_BeginDebugGroup("R_DrawWorldSurfaceLeafAnim");

	const auto& vTexChainList = pLeaf->vTextureChainList[WSURF_TEXCHAIN_LIST_ANIM];

	for (size_t i = 0; i < vTexChainList.size(); ++i)
	{
		auto& texchain = vTexChainList[i];

		auto texture = R_GetAnimatedTexture(texchain.texture);

		program_state_t WSurfProgramState = 0;

		if (g_WorldSurfaceRenderer.bDiffuseTexture)
		{
			WSurfProgramState |= WSURF_DIFFUSE_ENABLED;

			GL_BindTextureUnit(WSURF_BIND_DIFFUSE_TEXTURE, GL_TEXTURE_2D, texture->gl_texturenum);

			R_BeginDetailTexture(texture, &WSurfProgramState);
		}

		if (g_WorldSurfaceRenderer.bLightmapTexture)
		{
			if (r_draw_classify & DRAW_CLASSIFY_LIGHTMAP)
			{
				WSurfProgramState |= WSURF_LIGHTMAP_ENABLED;
			}

			if ((int)r_fullbright->value > 0 || !(*cl_worldmodel)->lightdata)
			{
				WSurfProgramState |= WSURF_FULLBRIGHT_ENABLED;
			}

			if ((*filterMode) != 0)
			{
				WSurfProgramState |= WSURF_COLOR_FILTER_ENABLED;
			}

			if (g_WorldSurfaceRenderer.iNumLegacyDLights)
			{
				WSurfProgramState |= WSURF_LEGACY_DLIGHT_ENABLED;
			}
		}
		else
		{
			WSurfProgramState |= WSURF_FULLBRIGHT_ENABLED;
		}

		if (R_IsRenderingWaterView())
		{
			WSurfProgramState |= WSURF_CLIP_WATER_ENABLED;
		}

		if (g_bPortalClipPlaneEnabled[0])
		{
			WSurfProgramState |= WSURF_CLIP_ENABLED;
		}

		if (R_IsRenderingGBuffer())
		{
			WSurfProgramState |= WSURF_GBUFFER_ENABLED;

			WSurfProgramState &= ~(WSURF_LEGACY_DLIGHT_ENABLED);
		}

		if ((*currententity)->curstate.rendermode != kRenderNormal && (*currententity)->curstate.rendermode != kRenderTransAlpha)
		{
			if ((*currententity)->curstate.rendermode == kRenderTransAdd || (*currententity)->curstate.rendermode == kRenderGlow)
				WSurfProgramState |= WSURF_ADDITIVE_BLEND_ENABLED;
			else
				WSurfProgramState |= WSURF_ALPHA_BLEND_ENABLED;
		}

		if ((*currententity)->curstate.rendermode == kRenderTransAlpha)
		{
			WSurfProgramState |= WSURF_ALPHA_SOLID_ENABLED;
		}

		if (!R_IsRenderingGBuffer())
		{
			if ((WSurfProgramState & WSURF_ADDITIVE_BLEND_ENABLED) && (int)r_fog_trans->value <= 1)
			{

			}
			else if ((WSurfProgramState & WSURF_ALPHA_BLEND_ENABLED) && (int)r_fog_trans->value <= 0)
			{

			}
			else
			{
				if (R_IsRenderingFog())
				{
					if (r_fog_mode == GL_LINEAR)
					{
						WSurfProgramState |= WSURF_LINEAR_FOG_ENABLED;
					}
					else if (r_fog_mode == GL_EXP)
					{
						WSurfProgramState |= WSURF_EXP_FOG_ENABLED;
					}
					else if (r_fog_mode == GL_EXP2)
					{
						WSurfProgramState |= WSURF_EXP2_FOG_ENABLED;
					}

					if (!R_IsRenderingGammaBlending() && r_linear_fog_shift->value > 0)
					{
						WSurfProgramState |= WSURF_LINEAR_FOG_SHIFT_ENABLED;
					}
				}
			}
		}

		if (R_IsRenderingGammaBlending())
		{
			WSurfProgramState |= WSURF_GAMMA_BLEND_ENABLED;
		}

		if (r_draw_oitblend && (WSurfProgramState & (WSURF_ALPHA_BLEND_ENABLED | WSURF_ADDITIVE_BLEND_ENABLED)))
		{
			WSurfProgramState |= WSURF_OIT_BLEND_ENABLED;
		}

		if (R_IsRenderingShadowView())
		{
			WSurfProgramState |= WSURF_SHADOW_CASTER_ENABLED;
		}

		R_DrawWorldSurfaceLeafBegin(pLeaf);

		GL_BeginStencilWrite(STENCIL_MASK_HAS_DECAL, STENCIL_MASK_HAS_DECAL);

		wsurf_program_t prog = { 0 };
		R_UseWSurfProgram(WSurfProgramState, &prog);

		glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, (void*)(texchain.startDrawOffset), texchain.drawCount, 0);

		(*c_brush_polys) += texchain.polyCount;

		R_EndDetailTexture(WSurfProgramState);

		GL_UseProgram(0);

		R_DrawWorldSurfaceLeafEnd();
	}

	GL_EndDebugGroup();

}//R_DrawWorldSurfaceLeafAnim

void R_DrawWorldSurfaceLeafSky(CWorldSurfaceModel* pModel, CWorldSurfaceLeaf* pLeaf)
{
	const auto& texchain = pLeaf->TextureChainSpecial[WSURF_TEXCHAIN_SPECIAL_SKY];

	if (!texchain.drawCount)
		return;

	GL_BeginDebugGroup("R_DrawWorldSurfaceLeafSky");

	auto texture = texchain.texture;

	program_state_t WSurfProgramState = 0;

	if (texture)
	{
		WSurfProgramState |= WSURF_DIFFUSE_ENABLED;

		GL_BindTextureUnit(WSURF_BIND_DIFFUSE_TEXTURE, GL_TEXTURE_2D, texture->gl_texturenum);
	}

	if (R_IsRenderingWaterView())
	{
		WSurfProgramState |= WSURF_CLIP_WATER_ENABLED;
	}

	if (g_bPortalClipPlaneEnabled[0])
	{
		WSurfProgramState |= WSURF_CLIP_ENABLED;
	}

	if (R_IsRenderingGBuffer())
	{
		WSurfProgramState |= WSURF_GBUFFER_ENABLED;

		WSurfProgramState &= ~(WSURF_LEGACY_DLIGHT_ENABLED);
	}

	if ((*currententity)->curstate.rendermode != kRenderNormal && (*currententity)->curstate.rendermode != kRenderTransAlpha)
	{
		if ((*currententity)->curstate.rendermode == kRenderTransAdd || (*currententity)->curstate.rendermode == kRenderGlow)
			WSurfProgramState |= WSURF_ADDITIVE_BLEND_ENABLED;
		else
			WSurfProgramState |= WSURF_ALPHA_BLEND_ENABLED;
	}

	if ((*currententity)->curstate.rendermode == kRenderTransAlpha)
	{
		WSurfProgramState |= WSURF_ALPHA_SOLID_ENABLED;
	}

	if ((int)r_wsurf_sky_fog->value > 0)
	{
		if (!R_IsRenderingGBuffer())
		{
			if ((WSurfProgramState & WSURF_ADDITIVE_BLEND_ENABLED) && (int)r_fog_trans->value <= 1)
			{

			}
			else if ((WSurfProgramState & WSURF_ALPHA_BLEND_ENABLED) && (int)r_fog_trans->value <= 0)
			{

			}
			else
			{
				if (R_IsRenderingFog())
				{
					if (r_fog_mode == GL_LINEAR)
					{
						WSurfProgramState |= WSURF_LINEAR_FOG_ENABLED;
					}
					else if (r_fog_mode == GL_EXP)
					{
						WSurfProgramState |= WSURF_EXP_FOG_ENABLED;
					}
					else if (r_fog_mode == GL_EXP2)
					{
						WSurfProgramState |= WSURF_EXP2_FOG_ENABLED;
					}

					if (!R_IsRenderingGammaBlending() && r_linear_fog_shift->value > 0)
					{
						WSurfProgramState |= WSURF_LINEAR_FOG_SHIFT_ENABLED;
					}
				}
			}
		}
	}

	if (R_IsRenderingGammaBlending())
	{
		WSurfProgramState |= WSURF_GAMMA_BLEND_ENABLED;
	}

	if (r_draw_oitblend && (WSurfProgramState & (WSURF_ALPHA_BLEND_ENABLED | WSURF_ADDITIVE_BLEND_ENABLED)))
	{
		WSurfProgramState |= WSURF_OIT_BLEND_ENABLED;
	}

	if (R_IsRenderingShadowView())
	{
		WSurfProgramState |= WSURF_SHADOW_CASTER_ENABLED;
	}

	R_DrawWorldSurfaceLeafBegin(pLeaf);

	wsurf_program_t prog = { 0 };
	R_UseWSurfProgram(WSurfProgramState, &prog);

	glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, (void*)(texchain.startDrawOffset), texchain.drawCount, 0);

	(*c_brush_polys) += texchain.polyCount;

	GL_UseProgram(0);

	R_DrawWorldSurfaceLeafEnd();

	GL_EndDebugGroup();

}//R_DrawWorldSurfaceLeafSky

float R_ScrollSpeed(void)
{
	float scrollSpeed = ((*currententity)->curstate.rendercolor.b + ((*currententity)->curstate.rendercolor.g << 8)) / 16.0;

	if ((*currententity)->curstate.rendercolor.r == 0)
		scrollSpeed = -scrollSpeed;

	scrollSpeed *= (*cl_time);

	return scrollSpeed;
}

void R_DrawWorldSurfaceModel(const std::shared_ptr<CWorldSurfaceModel>& pModel, cl_entity_t* ent)
{
	GL_BeginDebugGroupFormat("R_DrawWorldSurfaceModel - %s", ent->model ? ent->model->name : "<empty>");

	entity_ubo_t EntityUBO;
	Matrix4x4_Transpose(EntityUBO.entityMatrix, r_entity_matrix);
	memcpy(EntityUBO.color, r_entity_color, sizeof(vec4));
	EntityUBO.scrollSpeed = R_ScrollSpeed();
	EntityUBO.scale = 0;

	GL_UploadSubDataToUBO(g_WorldSurfaceRenderer.hEntityUBO, 0, sizeof(EntityUBO), &EntityUBO);

	glBindBufferBase(GL_UNIFORM_BUFFER, BINDING_POINT_ENTITY_UBO, g_WorldSurfaceRenderer.hEntityUBO);

	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, BINDING_POINT_MATERIAL_SSBO, g_WorldSurfaceRenderer.hMaterialSSBO);

	if (g_WorldSurfaceRenderer.bLightmapTexture)
	{
		for (int lightmap_idx = 0; lightmap_idx < MAXLIGHTMAPS; ++lightmap_idx)
		{
			if (g_WorldSurfaceRenderer.iLightmapTextureArray[lightmap_idx])
			{
				GL_BindTextureUnit(WSURF_BIND_LIGHTMAP_TEXTURE_0 + lightmap_idx, GL_TEXTURE_2D_ARRAY, g_WorldSurfaceRenderer.iLightmapTextureArray[lightmap_idx]);
			}
		}
	}

	std::shared_ptr<CWorldSurfaceLeaf> pLeaf;

	if (pModel->m_model == (*cl_worldmodel))
	{
		int leafIndex = R_GetWorldLeafIndex((*cl_worldmodel), (*r_viewleaf));

		pLeaf = pModel->GetLeafByIndex(leafIndex);

		if (!pLeaf)
		{
			R_GenerateWorldSurfaceModelLeaf(pModel, (*cl_worldmodel), (*r_viewleaf), true);

			pLeaf = pModel->GetLeafByIndex(leafIndex);
		}

		if (pLeaf && !pLeaf->hABO)
		{
			//Use previous leaf when current leaf not available
			pLeaf = g_WorldSurfaceRenderer.pCurrentWorldLeaf.lock();
		}

		//Always draw skybox before world, when rendering water view
		if (R_IsRenderingWaterView())
		{
			if (pLeaf)
			{
				if (R_WorldSurfaceLeafHasSky(pModel.get(), pLeaf.get()))
				{
					R_DrawSkyBox();
				}
			}
		}

		if (pLeaf && pLeaf->hABO)
		{
			if (R_IsRenderingWaterView())
			{
				auto pNoVisLeaf = pModel->GetLeafByIndex(0);

				if (!pNoVisLeaf)
				{
					R_GenerateWorldSurfaceModelLeaf(pModel, (*cl_worldmodel), nullptr, true);

					pNoVisLeaf = pModel->GetLeafByIndex(0);
				}

				if (pNoVisLeaf && !pNoVisLeaf->hABO)
				{
					//Use previous leaf when current leaf not available
					pNoVisLeaf = g_WorldSurfaceRenderer.pCurrentWaterLeaf.lock();
				}

				if (pNoVisLeaf && pNoVisLeaf->hABO)
				{
					glColorMask(0, 0, 0, 0);

					R_DrawWorldSurfaceLeafSky(pModel.get(), pNoVisLeaf.get());

					glColorMask(1, 1, 1, 1);

					R_DrawWorldSurfaceLeafStatic(pModel.get(), pNoVisLeaf.get());
					R_DrawWorldSurfaceLeafAnim(pModel.get(), pNoVisLeaf.get());

					g_WorldSurfaceRenderer.pCurrentWaterLeaf = pLeaf;
				}
			}
			else if (R_IsRenderingShadowView())
			{
				if (r_draw_classify & DRAW_CLASSIFY_WORLD)
				{
					if (pModel->m_pShadowProxyDraws.size() > 0)
						R_DrawWorldSurfaceModelShadowProxy(pModel.get());
					else
						R_DrawWorldSurfaceLeafShadow(pLeaf.get(), false);
				}
			}
			else
			{
				if (r_draw_classify & DRAW_CLASSIFY_WORLD)
				{
					R_DrawWorldSurfaceLeafStatic(pModel.get(), pLeaf.get());
					R_DrawWorldSurfaceLeafAnim(pModel.get(), pLeaf.get());
				}

				g_WorldSurfaceRenderer.pCurrentWorldLeaf = pLeaf;
			}
		}

		if (!R_IsRenderingWaterView())
		{
			if (pLeaf)
			{
				if (R_WorldSurfaceLeafHasSky(pModel.get(), pLeaf.get()))
				{
					R_DrawSkyBox();
				}
			}
		}
	}
	else
	{
		pLeaf = pModel->GetLeafByIndex(0);

		if (!pLeaf)
		{
			R_GenerateWorldSurfaceModelLeaf(pModel, pModel->m_model, nullptr, false);

			pLeaf = pModel->GetLeafByIndex(0);
		}

		if (pLeaf && pLeaf->hABO)
		{
			if (R_IsRenderingShadowView())
			{
				if (pModel->m_pShadowProxyDraws.size() > 0)
					R_DrawWorldSurfaceModelShadowProxy(pModel.get());
				else
					R_DrawWorldSurfaceLeafShadow(pLeaf.get(), false);
			}
			else
			{
				R_DrawWorldSurfaceLeafStatic(pModel.get(), pLeaf.get());
				R_DrawWorldSurfaceLeafAnim(pModel.get(), pLeaf.get());
			}
		}
	}

	R_DrawDecals(ent);

	if (g_WorldSurfaceRenderer.bLightmapTexture)
	{
		for (int lightmap_idx = 0; lightmap_idx < MAXLIGHTMAPS; ++lightmap_idx)
		{
			if (g_WorldSurfaceRenderer.iLightmapTextureArray[lightmap_idx])
			{
				GL_BindTextureUnit(WSURF_BIND_LIGHTMAP_TEXTURE_0 + lightmap_idx, GL_TEXTURE_2D_ARRAY, 0);
			}
		}
	}

	if (pLeaf)
	{
		R_DrawWaters(pModel.get(), pLeaf.get(), ent);
	}

	GL_EndDebugGroup();
}

void R_InitWSurf(void)
{
	R_ClearBSPEntities();
}

void R_FreeWorldResources(void)
{
	g_WorldSurfaceRenderer.pCurrentWorldLeaf.reset();
	g_WorldSurfaceRenderer.pCurrentWaterLeaf.reset();
	g_WorldSurfaceRenderer.vWorldMaterials.clear();
	g_WorldSurfaceRenderer.vWorldMaterialTextureMapping.clear();

	R_ClearDecalCache();
	R_ClearDetailTextureCache();

	R_ClearBSPEntities();

	R_FreeLightmapTextures();

	R_ClearWorldSurfaceWorldModels();
	R_ClearWorldSurfaceModels();
}

void R_LoadWorldResources(void)
{
	R_LoadMapDetailTextures();
	R_LoadBaseDetailTextures();
	R_LoadBaseDecalTextures();

	std::vector<bspentity_t*> vBSPEntities;

	R_ParseBSPEntities((*cl_worldmodel)->entities, vBSPEntities);
	R_LoadExternalEntities(vBSPEntities);
	R_LoadBSPEntities(vBSPEntities);

	for (auto ent : vBSPEntities)
	{
		delete ent;
	}

	for (int j = 1; j < EngineGetMaxClientModels(); j++)
	{
		auto mod = gEngfuncs.hudGetModelByIndex(j);

		if (!mod)
			break;

		if (mod->type == mod_brush)
		{
			R_GetWorldSurfaceModel(mod);
		}
	}

	GL_UploadDataToSSBOStaticDraw(g_WorldSurfaceRenderer.hMaterialSSBO, sizeof(world_material_t) * g_WorldSurfaceRenderer.vWorldMaterials.size(), g_WorldSurfaceRenderer.vWorldMaterials.data());
}

void R_ShutdownWSurf(void)
{
	g_WSurfProgramTable.clear();

	R_FreeWorldResources();

	R_FreeSceneUBO();
}

const char* s_WorldSurfaceTextureTypeNames[] = {
	"WSURF_DIFFUSE_TEXTURE",
	"WSURF_DETAIL_TEXTURE",
	"WSURF_NORMAL_TEXTURE",
	"WSURF_PARALLAX_TEXTURE",
	"WSURF_SPECULAR_TEXTURE",
};

void R_LoadDecalTextures(const char* pFileContent)
{
	auto ptext = pFileContent;

	while (1)
	{
		char temp[256];
		char basetexture[256];
		char detailtexture[256];
		char sz_xscale[64];
		char sz_yscale[64];

		ptext = gEngfuncs.COM_ParseFile(ptext, basetexture);

		if (!ptext)
			break;

		if ((basetexture[0] == '{' || basetexture[0] == '}'))
		{
			ptext = gEngfuncs.COM_ParseFile(ptext, temp);
			strcat(basetexture, temp);

			if (!ptext)
				break;
		}

		ptext = gEngfuncs.COM_ParseFile(ptext, detailtexture);
		if (!ptext)
			break;

		ptext = gEngfuncs.COM_ParseFile(ptext, sz_xscale);

		if (!ptext)
			break;

		if ((sz_xscale[0] == '{' || sz_xscale[0] == '}'))
		{
			strcat(detailtexture, sz_xscale);

			ptext = gEngfuncs.COM_ParseFile(ptext, temp);
			if (!ptext)
				break;

			strcat(detailtexture, temp);

			ptext = gEngfuncs.COM_ParseFile(ptext, sz_xscale);
			if (!ptext)
				break;
		}

		ptext = gEngfuncs.COM_ParseFile(ptext, sz_yscale);
		if (!ptext)
			break;

		//Default: load as diffuse texture
		int texType = WSURF_DIFFUSE_TEXTURE;

		std::string base = basetexture;

		if (base.length() > (sizeof("_REPLACE") - 1) && !strcmp(&base[base.length() - (sizeof("_REPLACE") - 1)], "_REPLACE"))
		{
			base = base.substr(0, base.length() - (sizeof("_REPLACE") - 1));
			texType = WSURF_DIFFUSE_TEXTURE;
		}
		else if (base.length() > (sizeof("_DIFFUSE") - 1) && !strcmp(&base[base.length() - (sizeof("_DIFFUSE") - 1)], "_DIFFUSE"))
		{
			base = base.substr(0, base.length() - (sizeof("_DIFFUSE") - 1));
			texType = WSURF_DIFFUSE_TEXTURE;
		}
		else if (base.length() > (sizeof("_PARALLAX") - 1) && !strcmp(&base[base.length() - (sizeof("_PARALLAX") - 1)], "_PARALLAX"))
		{
			base = base.substr(0, base.length() - (sizeof("_PARALLAX") - 1));
			texType = WSURF_PARALLAX_TEXTURE;
		}
		else if (base.length() > (sizeof("_NORMAL") - 1) && !strcmp(&base[base.length() - (sizeof("_NORMAL") - 1)], "_NORMAL"))
		{
			base = base.substr(0, base.length() - (sizeof("_NORMAL") - 1));
			texType = WSURF_NORMAL_TEXTURE;
		}
		else if (base.length() > (sizeof("_DETAIL") - 1) && !strcmp(&base[base.length() - (sizeof("_DETAIL") - 1)], "_DETAIL"))
		{
			base = base.substr(0, base.length() - (sizeof("_DETAIL") - 1));
			texType = WSURF_DETAIL_TEXTURE;
		}
		else if (base.length() > (sizeof("_SPECULAR") - 1) && !strcmp(&base[base.length() - (sizeof("_SPECULAR") - 1)], "_SPECULAR"))
		{
			base = base.substr(0, base.length() - (sizeof("_SPECULAR") - 1));
			texType = WSURF_SPECULAR_TEXTURE;
		}

		float i_xscale = atof(sz_xscale);
		float i_yscale = atof(sz_yscale);

		std::shared_ptr<CWorldSurfaceRenderMaterial> pRenderMaterial;

		uint32_t textureHash = R_GetWorldTextureHash(base.c_str());

		auto itor = g_DecalTextureRenderMaterials.find(textureHash);

		if (itor != g_DecalTextureRenderMaterials.end())
		{
			pRenderMaterial = itor->second;
		}
		else
		{
			pRenderMaterial = std::make_shared<CWorldSurfaceRenderMaterial>(base);

			g_DecalTextureRenderMaterials[textureHash] = pRenderMaterial;
		}

		if (pRenderMaterial)
		{
			if (pRenderMaterial->textures[texType].gltexturenum)
			{
				gEngfuncs.Con_DPrintf("R_LoadDecalTextures: \"%s\" already exists for basetexture \"%s\".\n", s_WorldSurfaceTextureTypeNames[texType], base.c_str());
				continue;
			}

			bool bLoaded = false;
			gl_loadtexture_result_t loadResult;
			std::string texturePath;

			//Texture name starts with "maps\\" or "maps/"
			if (!bLoaded &&
				!strnicmp(detailtexture, "maps", sizeof("maps") - 1) &&
				(detailtexture[sizeof("maps") - 1] == '\\' || detailtexture[sizeof("maps") - 1] == '/'))
			{
				texturePath = detailtexture;
				if (!V_GetFileExtension(detailtexture))
					texturePath += ".tga";

				bLoaded = R_LoadTextureFromFile(texturePath.c_str(), texturePath.c_str(), GLT_WORLD, true, &loadResult);
			}

			//Search under gfx
			if (!bLoaded)
			{
				texturePath = "gfx/";
				texturePath += detailtexture;
				if (!V_GetFileExtension(detailtexture))
					texturePath += ".tga";

				bLoaded = R_LoadTextureFromFile(texturePath.c_str(), texturePath.c_str(), GLT_WORLD, true, &loadResult);
			}

			//Search under renderer/texture
			if (!bLoaded)
			{
				texturePath = "renderer/texture/";
				texturePath += detailtexture;
				if (!V_GetFileExtension(detailtexture))
					texturePath += ".tga";

				bLoaded = R_LoadTextureFromFile(texturePath.c_str(), texturePath.c_str(), GLT_WORLD, true, &loadResult);
			}

			if (!bLoaded)
			{
				gEngfuncs.Con_DPrintf("R_LoadDecalTextures: Failed to load \"%s\" for basetexture \"%s\".\n", detailtexture, base.c_str());
				continue;
			}

			pRenderMaterial->textures[texType].gltexturenum = loadResult.gltexturenum;
			pRenderMaterial->textures[texType].width = loadResult.width;
			pRenderMaterial->textures[texType].height = loadResult.height;

			if(i_xscale != 0)
				pRenderMaterial->textures[texType].scaleX = i_xscale;

			if (i_yscale != 0)
				pRenderMaterial->textures[texType].scaleY = i_yscale;
		}
	}
}

void R_LoadBaseDecalTextures(void)
{
	char* pfile = (char*)gEngfuncs.COM_LoadFile("renderer/decal_textures.txt", 5, NULL);

	if (!pfile)
	{
		gEngfuncs.Con_DPrintf("R_LoadBaseDecalTextures: No decal texture file \"renderer/decal_textures.txt\"\n");
		return;
	}

	R_LoadDecalTextures(pfile);

	gEngfuncs.COM_FreeFile(pfile);
}

void R_LoadDetailTextures(const char* pFileContent)
{
	auto ptext = pFileContent;

	while (1)
	{
		char temp[256];
		char basetexture[256];
		char detailtexture[256];
		char sz_xscale[64];
		char sz_yscale[64];

		ptext = gEngfuncs.COM_ParseFile(ptext, basetexture);

		if (!ptext)
			break;

		if ((basetexture[0] == '{' || basetexture[0] == '}'))
		{
			ptext = gEngfuncs.COM_ParseFile(ptext, temp);
			strcat(basetexture, temp);

			if (!ptext)
				break;
		}

		ptext = gEngfuncs.COM_ParseFile(ptext, detailtexture);
		if (!ptext)
			break;

		ptext = gEngfuncs.COM_ParseFile(ptext, sz_xscale);

		if (!ptext)
			break;

		if ((sz_xscale[0] == '{' || sz_xscale[0] == '}'))
		{
			strcat(detailtexture, sz_xscale);

			ptext = gEngfuncs.COM_ParseFile(ptext, temp);

			if (!ptext)
				break;

			strcat(detailtexture, temp);

			ptext = gEngfuncs.COM_ParseFile(ptext, sz_xscale);

			if (!ptext)
				break;
		}

		ptext = gEngfuncs.COM_ParseFile(ptext, sz_yscale);

		if (!ptext)
			break;

		//Default: load as detail texture
		int texType = WSURF_DETAIL_TEXTURE;

		std::string base = basetexture;

		if (base.length() > (sizeof("_PARALLAX") - 1) && !strcmp(&base[base.length() - (sizeof("_PARALLAX") - 1)], "_PARALLAX"))
		{
			base = base.substr(0, base.length() - (sizeof("_PARALLAX") - 1));
			texType = WSURF_PARALLAX_TEXTURE;
		}
		else if (base.length() > (sizeof("_NORMAL") - 1) && !strcmp(&base[base.length() - (sizeof("_NORMAL") - 1)], "_NORMAL"))
		{
			base = base.substr(0, base.length() - (sizeof("_NORMAL") - 1));
			texType = WSURF_NORMAL_TEXTURE;
		}
		else if (base.length() > (sizeof("_REPLACE") - 1) && !strcmp(&base[base.length() - (sizeof("_REPLACE") - 1)], "_REPLACE"))
		{
			base = base.substr(0, base.length() - (sizeof("_REPLACE") - 1));
			texType = WSURF_DIFFUSE_TEXTURE;
		}
		else if (base.length() > (sizeof("_DIFFUSE") - 1) && !strcmp(&base[base.length() - (sizeof("_DIFFUSE") - 1)], "_DIFFUSE"))
		{
			base = base.substr(0, base.length() - (sizeof("_DIFFUSE") - 1));
			texType = WSURF_DIFFUSE_TEXTURE;
		}
		else if (base.length() > (sizeof("_DETAIL") - 1) && !strcmp(&base[base.length() - (sizeof("_DETAIL") - 1)], "_DETAIL"))
		{
			base = base.substr(0, base.length() - (sizeof("_DETAIL") - 1));
			texType = WSURF_DETAIL_TEXTURE;
		}
		else if (base.length() > (sizeof("_SPECULAR") - 1) && !strcmp(&base[base.length() - (sizeof("_SPECULAR") - 1)], "_SPECULAR"))
		{
			base = base.substr(0, base.length() - (sizeof("_SPECULAR") - 1));
			texType = WSURF_SPECULAR_TEXTURE;
		}

		auto glt = GL_FindTexture(base.c_str(), GLT_WORLD, NULL, NULL);

		if (!glt)
		{
			gEngfuncs.Con_DPrintf("R_LoadDetailTextures: Missing basetexture \"%s\".\n", base.c_str());
			continue;
		}

		float i_xscale = atof(sz_xscale);
		float i_yscale = atof(sz_yscale);

		std::shared_ptr<CWorldSurfaceRenderMaterial> pRenderMaterial;

		for (auto it : g_WorldTextureRenderMaterials)
		{
			if (it.second->basetexture == base)
			{
				pRenderMaterial = it.second;
			}
		}

		if (!pRenderMaterial)
		{
			pRenderMaterial = std::make_shared<CWorldSurfaceRenderMaterial>(base);

			g_WorldTextureRenderMaterials[glt] = pRenderMaterial;
		}

		if (pRenderMaterial)
		{
			if (pRenderMaterial->textures[texType].gltexturenum)
			{
				gEngfuncs.Con_DPrintf("R_LoadDetailTextures: \"%s\" already exists for basetexture \"%s\".\n", s_WorldSurfaceTextureTypeNames[texType], base.c_str());
				continue;
			}

			bool bLoaded = false;
			gl_loadtexture_result_t loadResult;
			std::string texturePath;

			//Texture name starts with "maps\\" or "maps/"
			if (!bLoaded &&
				!strnicmp(detailtexture, "maps", sizeof("maps") - 1) &&
				(detailtexture[sizeof("maps") - 1] == '\\' || detailtexture[sizeof("maps") - 1] == '/'))
			{
				texturePath = detailtexture;
				if (!V_GetFileExtension(detailtexture))
					texturePath += ".tga";

				bLoaded = R_LoadTextureFromFile(texturePath.c_str(), texturePath.c_str(), GLT_WORLD, true, &loadResult);
			}

			//Search under gfx
			if (!bLoaded)
			{
				texturePath = "gfx/";
				texturePath += detailtexture;
				if (!V_GetFileExtension(detailtexture))
					texturePath += ".tga";

				bLoaded = R_LoadTextureFromFile(texturePath.c_str(), texturePath.c_str(), GLT_WORLD, true, &loadResult);
			}

			//Search under renderer/texture
			if (!bLoaded)
			{
				texturePath = "renderer/texture/";
				texturePath += detailtexture;
				if (!V_GetFileExtension(detailtexture))
					texturePath += ".tga";

				bLoaded = R_LoadTextureFromFile(texturePath.c_str(), texturePath.c_str(), GLT_WORLD, true, &loadResult);
			}

			if (!bLoaded)
			{
				gEngfuncs.Con_DPrintf("R_LoadDetailTextures: Failed to load \"%s\" for basetexture %s\n", detailtexture, base.c_str());
				continue;
			}

			pRenderMaterial->textures[texType].gltexturenum = loadResult.gltexturenum;
			pRenderMaterial->textures[texType].width = loadResult.width;
			pRenderMaterial->textures[texType].height = loadResult.height;
			pRenderMaterial->textures[texType].scaleX = i_xscale;
			pRenderMaterial->textures[texType].scaleY = i_yscale;
		}
	}
}

void R_LoadBaseDetailTextures(void)
{
	char* pfile = (char*)gEngfuncs.COM_LoadFile("renderer/detail_textures.txt", 5, NULL);
	if (!pfile)
	{
		gEngfuncs.Con_DPrintf("R_LoadBaseDetailTextures: No detail texture file \"renderer/detail_textures.txt\"\n");
		return;
	}

	R_LoadDetailTextures(pfile);

	gEngfuncs.COM_FreeFile(pfile);
}

void R_LoadMapDetailTextures(void)
{
	std::string name = gEngfuncs.pfnGetLevelName();

	RemoveFileExtension(name);

	name += "_detail.txt";

	char* pfile = (char*)gEngfuncs.COM_LoadFile(name.c_str(), 5, NULL);
	if (!pfile)
	{
		gEngfuncs.Con_DPrintf("R_LoadMapDetailTextures: No detail texture file %s\n", name.c_str());
		return;
	}

	R_LoadDetailTextures(pfile);

	gEngfuncs.COM_FreeFile(pfile);
}

uint32_t R_GetWorldTextureHash(const char *name)
{
	uint32_t seed = 0;
	uint32_t result = MurmurHash2(name, strlen(name), seed);

	return result;
}

uint32_t R_GetWorldTextureHash(texture_t* ptexture)
{
	return R_GetWorldTextureHash(ptexture->name);
}

std::shared_ptr<CWorldSurfaceRenderMaterial> R_GetRenderMaterialForDecalTexture(const char * decalname)
{
	auto textureHash = R_GetWorldTextureHash(decalname);

	auto itor = g_DecalTextureRenderMaterials.find(textureHash);

	if (itor != g_DecalTextureRenderMaterials.end())
	{
		const auto& pRenderMaterial = itor->second;

		if (pRenderMaterial)
		{
			return pRenderMaterial;
		}
	}

	return nullptr;
}

std::shared_ptr<CWorldSurfaceRenderMaterial> R_GetRenderMaterialForWorldTexture(texture_t *ptexture)
{
	auto textureHash = R_GetWorldTextureHash(ptexture);

	auto itor = g_WorldTextureRenderMaterials.find(textureHash);

	if (itor != g_WorldTextureRenderMaterials.end())
	{
		const auto& pRenderMaterial = itor->second;

		if (pRenderMaterial)
		{
			return pRenderMaterial;
		}
	}

	return nullptr;
}

void R_BeginDetailTextureFromRenderMaterial(CWorldSurfaceRenderMaterial* pRenderMaterial, program_state_t* WSurfProgramState)
{
	if (pRenderMaterial->textures[WSURF_DIFFUSE_TEXTURE].gltexturenum)
	{
		GL_BindTextureUnit(WSURF_BIND_DIFFUSE_TEXTURE, GL_TEXTURE_2D, pRenderMaterial->textures[WSURF_DIFFUSE_TEXTURE].gltexturenum);
	}

	if (pRenderMaterial->textures[WSURF_DETAIL_TEXTURE].gltexturenum)
	{
		GL_BindTextureUnit(WSURF_BIND_DETAIL_TEXTURE, GL_TEXTURE_2D, pRenderMaterial->textures[WSURF_DETAIL_TEXTURE].gltexturenum);

		if (WSurfProgramState)
			(*WSurfProgramState) |= WSURF_DETAILTEXTURE_ENABLED;
	}

	if (pRenderMaterial->textures[WSURF_NORMAL_TEXTURE].gltexturenum)
	{
		GL_BindTextureUnit(WSURF_BIND_NORMAL_TEXTURE, GL_TEXTURE_2D, pRenderMaterial->textures[WSURF_NORMAL_TEXTURE].gltexturenum);

		if (WSurfProgramState)
			(*WSurfProgramState) |= WSURF_NORMALTEXTURE_ENABLED;
	}

	if (pRenderMaterial->textures[WSURF_PARALLAX_TEXTURE].gltexturenum)
	{
		GL_BindTextureUnit(WSURF_BIND_PARALLAX_TEXTURE, GL_TEXTURE_2D, pRenderMaterial->textures[WSURF_PARALLAX_TEXTURE].gltexturenum);

		if (WSurfProgramState)
			(*WSurfProgramState) |= WSURF_PARALLAXTEXTURE_ENABLED;
	}

	if (pRenderMaterial->textures[WSURF_SPECULAR_TEXTURE].gltexturenum)
	{
		GL_BindTextureUnit(WSURF_BIND_SPECULAR_TEXTURE, GL_TEXTURE_2D, pRenderMaterial->textures[WSURF_SPECULAR_TEXTURE].gltexturenum);

		if (WSurfProgramState)
			(*WSurfProgramState) |= WSURF_SPECULARTEXTURE_ENABLED;
	}
}

void R_BeginDetailTexture(texture_t *ptexture, program_state_t* WSurfProgramState)
{
	const auto& pRenderMaterial = R_GetRenderMaterialForWorldTexture(ptexture);

	if (pRenderMaterial)
	{
		R_BeginDetailTextureFromRenderMaterial(pRenderMaterial.get(), WSurfProgramState);
	}
}

void R_EndDetailTexture(program_state_t WSurfProgramState)
{
	if (WSurfProgramState & WSURF_DETAILTEXTURE_ENABLED)
	{
		GL_BindTextureUnit(WSURF_BIND_DETAIL_TEXTURE, GL_TEXTURE_2D, 0);
	}

	if (WSurfProgramState & WSURF_NORMALTEXTURE_ENABLED)
	{
		GL_BindTextureUnit(WSURF_BIND_NORMAL_TEXTURE, GL_TEXTURE_2D, 0);
	}

	if (WSurfProgramState & WSURF_PARALLAXTEXTURE_ENABLED)
	{
		GL_BindTextureUnit(WSURF_BIND_PARALLAX_TEXTURE, GL_TEXTURE_2D, 0);
	}

	if (WSurfProgramState & WSURF_SPECULARTEXTURE_ENABLED)
	{
		GL_BindTextureUnit(WSURF_BIND_SPECULAR_TEXTURE, GL_TEXTURE_2D, 0);
	}
}

const char* ValueForKey(bspentity_t* ent, const char* key)
{
	for (epair_t* pEPair = ent->epairs; pEPair; pEPair = pEPair->next)
	{
		if (!strcmp(pEPair->key, key))
			return pEPair->value;
	}
	return NULL;
}

const char* ValueForKeyEx(bspentity_t* ent, const char* key, epair_t** ppLastEPair)
{
	if ((*ppLastEPair))
	{
		for (epair_t* pEPair = (*ppLastEPair)->next; pEPair; pEPair = pEPair->next)
		{
			if (!strcmp(pEPair->key, key))
			{
				(*ppLastEPair) = pEPair;
				return pEPair->value;
			}
		}

	}
	else
	{
		for (epair_t* pEPair = ent->epairs; pEPair; pEPair = pEPair->next)
		{
			if (!strcmp(pEPair->key, key))
			{
				(*ppLastEPair) = pEPair;
				return pEPair->value;
			}
		}
	}
	*ppLastEPair = NULL;
	return NULL;
}

void ValueForKeyExArray(bspentity_t* ent, const char* key, std::vector<const char*>& strArray)
{
	const char* flags = NULL;
	epair_t* ep = NULL;
	do
	{
		flags = ValueForKeyEx(ent, "flags", &ep);

		if (flags) {
			strArray.emplace_back(flags);
		}

	} while (flags);
}

void R_ClearBSPEntities()
{
	g_WorldSurfaceShadowProxyModels.clear();
	r_flashlight_cone_texture_name.clear();
	g_EnvWaterControls.clear();
	g_DynamicLights.clear();
}

static bspentity_t* current_parse_entity = NULL;
static char com_token[4096];

static bool R_ParseBSPEntityKeyValue(
	const char* classname,
	const char* keyname,
	const char* value,
	std::vector<bspentity_t*>& vBSPEntities)
{
	if (classname == NULL)
	{
		current_parse_entity = new bspentity_t;

		vBSPEntities.emplace_back(current_parse_entity);

		if (!current_parse_entity)
			return false;

		current_parse_entity->classname = NULL;
		current_parse_entity->epairs = NULL;
		VectorClear(current_parse_entity->origin);
	}

	if (current_parse_entity)
	{
		auto epairs = new epair_t;
		auto keynamelen = strlen(keyname);
		epairs->key = new char[keynamelen + 1];
		strncpy(epairs->key, keyname, keynamelen);
		epairs->key[keynamelen] = 0;

		auto valuelen = strlen(value);
		epairs->value = new char[valuelen + 1];
		strncpy(epairs->value, value, valuelen);
		epairs->value[valuelen] = 0;

		if (!strcmp(keyname, "origin"))
		{
			sscanf(value, "%f %f %f", &current_parse_entity->origin[0], &current_parse_entity->origin[1], &current_parse_entity->origin[2]);
		}

		if (!strcmp(keyname, "classname"))
		{
			current_parse_entity->classname = epairs->value;
		}

		epairs->next = current_parse_entity->epairs;
		current_parse_entity->epairs = epairs;

		return true;
	}

	return false;
}

static bool R_ParseBSPEntityClassname(
	const char* szInputStream,
	char* classname,
	size_t classname_len,
	std::vector<bspentity_t*>& vBSPEntities)
{
	char szKeyName[256];

	// key
	szInputStream = gEngfuncs.COM_ParseFile(szInputStream, com_token);
	while (szInputStream && com_token[0] != '}')
	{
		strncpy(szKeyName, com_token, sizeof(szKeyName) - 1);
		szKeyName[sizeof(szKeyName) - 1] = 0;

		szInputStream = gEngfuncs.COM_ParseFile(szInputStream, com_token);

		if (!strcmp(szKeyName, "classname"))
		{
			R_ParseBSPEntityKeyValue(NULL, szKeyName, com_token, vBSPEntities);

			strncpy(classname, com_token, classname_len);
			classname[classname_len - 1] = 0;

			return true;
		}

		if (!szInputStream)
		{
			break;
		}

		szInputStream = gEngfuncs.COM_ParseFile(szInputStream, com_token);
	}

	return false;
}

static const char* R_ParseBSPEntity(
	const char* data,
	std::vector<bspentity_t*>& vBSPEntities)
{
	char keyname[256] = { 0 };
	char classname[256] = { 0 };

	if (R_ParseBSPEntityClassname(data, classname, sizeof(classname), vBSPEntities))
	{
		while (1)
		{
			data = gEngfuncs.COM_ParseFile(data, com_token);

			if (com_token[0] == '}')
			{
				break;
			}

			if (!data)
			{
				Sys_Error("R_ParseBSPEntity: EOF without closing brace");
			}

			strncpy(keyname, com_token, sizeof(keyname) - 1);
			keyname[sizeof(keyname) - 1] = 0;
			// Remove tail spaces
			for (int n = strlen(keyname) - 1; n >= 0 && keyname[n] == ' '; n--)
			{
				keyname[n] = 0;
			}

			data = gEngfuncs.COM_ParseFile(data, com_token);

			if (!data)
			{
				Sys_Error("R_ParseBSPEntity: EOF without closing brace");
			}

			if (com_token[0] == '}')
			{
				Sys_Error("R_ParseBSPEntity: closing brace without data");
			}

			if (!strcmp(classname, com_token))
			{
				continue;
			}

			R_ParseBSPEntityKeyValue(classname, keyname, com_token, vBSPEntities);
		}
	}
	else
	{
		gEngfuncs.Con_Printf("R_ParseBSPEntity: missing classname, try next section.");

		while (1)
		{
			data = gEngfuncs.COM_ParseFile(data, com_token);
			if (!data)
			{
				break;
			}
			if (com_token[0] == '}')
			{
				break;
			}
		}
	}

	current_parse_entity = NULL;

	return data;
}

void R_ParseBSPEntities(const char* data, std::vector<bspentity_t*>& vBSPEntities)
{
	while (1)
	{
		data = gEngfuncs.COM_ParseFile(data, com_token);

		if (!data)
		{
			break;
		}

		if (com_token[0] != '{')
		{
			Sys_Error("R_ParseBSPEntities: found %s when expecting {", com_token);
			return;
		}

		data = R_ParseBSPEntity(data, vBSPEntities);
	}
}

void R_LoadExternalEntities(std::vector<bspentity_t*>& vBSPEntities)
{
	std::string fullPath = gEngfuncs.pfnGetLevelName();

	RemoveFileExtension(fullPath);

	fullPath += "_entity.txt";

	auto pFile = (char*)gEngfuncs.COM_LoadFile(fullPath.c_str(), 5, NULL);

	if (!pFile)
	{
		fullPath = "renderer/default_entity.txt";

		pFile = (char*)gEngfuncs.COM_LoadFile(fullPath.c_str(), 5, NULL);

		if (!pFile)
		{
			gEngfuncs.Con_DPrintf("R_LoadExternalEntities: Could not load \"%s\".\n", fullPath);
			return;
		}
	}

	R_ParseBSPEntities(pFile, vBSPEntities);

	gEngfuncs.COM_FreeFile(pFile);
}

#if 0
void R_ParseBSPEntity_Env_Cubemap(bspentity_t* ent)
{
	float temp[4];

	cubemap_t cubemap;
	cubemap.cubetex = 0;
	cubemap.size = 0;
	cubemap.radius = 0;
	cubemap.origin[0] = 0;
	cubemap.origin[1] = 0;
	cubemap.origin[2] = 0;
	cubemap.extension = "tga";

	auto name_string = ValueForKey(ent, "name");
	if (name_string)
	{
		cubemap.name = name_string;
	}

	auto origin_string = ValueForKey(ent, "origin");
	if (origin_string)
	{
		if (sscanf(origin_string, "%f %f %f", &temp[0], &temp[1], &temp[2]) == 3)
		{
			cubemap.origin[0] = temp[0];
			cubemap.origin[1] = temp[1];
			cubemap.origin[2] = temp[2];
		}
		else
		{
			gEngfuncs.Con_Printf("R_LoadBSPEntities: Failed to parse \"origin\" in entity \"env_cubemap\"\n");
		}
	}

	auto cubemapsize_string = ValueForKey(ent, "cubemapsize");
	if (cubemapsize_string)
	{
		int size = 0;
		if (sscanf(cubemapsize_string, "%d", &size) == 1)
		{
			cubemap.size = size;
		}
		else
		{
			gEngfuncs.Con_Printf("R_LoadBSPEntities: Failed to parse \"cubemapsize\" in entity \"env_cubemap\"\n");
		}
	}

	auto radius_string = ValueForKey(ent, "radius");
	if (radius_string)
	{
		if (sscanf(radius_string, "%f", &temp[0]) == 1 && temp[0] > 0)
		{
			cubemap.radius = temp[0];
		}
		else
		{
			gEngfuncs.Con_Printf("R_LoadBSPEntities: Failed to parse \"radius\" in entity \"env_cubemap\"\n");
		}
	}

	auto extension_string = ValueForKey(ent, "extension");
	if (extension_string)
	{
		cubemap.extension = extension_string;
	}

	if (cubemap.name.length() > 0 && cubemap.radius > 0)
	{
		R_LoadCubemap(&cubemap);

		r_cubemaps.emplace_back(cubemap);
	}
}
#endif

void R_ParseBSPEntity_Light_Dynamic(bspentity_t* ent)
{
	/*
		Example:

		{
			"origin" "-30 68 72"
			"size" "1024.0"
			"color" "192 192 192"
			"classname" "light_dynamic"
			"type" "directional"
			"ambient" "0.1"
			"diffuse" "1.0"
			"specular" "1.0"
			"specularpow" "10.0"
			"shadow" "1"
		}
	*/
	auto dynlight = std::make_shared<CDynamicLight>();

	auto type_string = ValueForKey(ent, "type");
	if (type_string)
	{
		if (!strcmp(type_string, "point"))
		{
			dynlight->type = DynamicLightType_Point;
		}
		else if(!strcmp(type_string, "spot"))
		{
			dynlight->type = DynamicLightType_Spot;
		}
		else if (!strcmp(type_string, "directional"))
		{
			dynlight->type = DynamicLightType_Directional;
		}
		else
		{
			dynlight->type = (DynamicLightType)atoi(type_string);
		}
	}

#define PARSE_KEY_VALUE_STRING(name, parser) auto name##_string = ValueForKey(ent, #name);\
	if (name##_string)\
	{\
		if (parser(name##_string, dynlight->name))\
		{\
		}\
		else\
		{\
			gEngfuncs.Con_Printf("R_LoadBSPEntities: Failed to parse \" #name \" in entity \"light_dynamic\"\n");\
		}\
	}

#define PARSE_KEY_VALUE_STRING_WRITEREF(name, parser) auto name##_string = ValueForKey(ent, #name);\
	if (name##_string)\
	{\
		if (parser(name##_string, &dynlight->name))\
		{\
		}\
		else\
		{\
			gEngfuncs.Con_Printf("R_LoadBSPEntities: Failed to parse \" #name \" in entity \"light_dynamic\"\n");\
		}\
	}
#define PARSE_KEY_VALUE_STRING_WRITEINT(name) auto name##_string = ValueForKey(ent, #name);\
	if (name##_string)\
	{\
		dynlight->name = atoi(name##_string);\
	}

	PARSE_KEY_VALUE_STRING(origin, UTIL_ParseStringAsVector3);
	PARSE_KEY_VALUE_STRING(angles, UTIL_ParseStringAsVector3);
	PARSE_KEY_VALUE_STRING(color, UTIL_ParseStringAsColor3);
	PARSE_KEY_VALUE_STRING_WRITEREF(size, UTIL_ParseStringAsVector1);
	PARSE_KEY_VALUE_STRING_WRITEREF(distance, UTIL_ParseStringAsVector1);
	PARSE_KEY_VALUE_STRING_WRITEREF(ambient, UTIL_ParseStringAsVector1);
	PARSE_KEY_VALUE_STRING_WRITEREF(diffuse, UTIL_ParseStringAsVector1);
	PARSE_KEY_VALUE_STRING_WRITEREF(specular, UTIL_ParseStringAsVector1);
	PARSE_KEY_VALUE_STRING_WRITEREF(specularpow, UTIL_ParseStringAsVector1);

	PARSE_KEY_VALUE_STRING_WRITEINT(shadow);
	PARSE_KEY_VALUE_STRING_WRITEINT(follow_player);

#undef PARSE_KEY_VALUE_STRING
#undef PARSE_KEY_VALUE_STRING_WRITEREF
#undef PARSE_KEY_VALUE_STRING_WRITEINT

	g_DynamicLights.emplace_back(dynlight);
}

void R_ParseBSPEntity_Env_Water_Control(bspentity_t* ent)
{
	auto pWaterControl = std::make_shared<CEnvWaterControl>();

	auto basetexture_string = ValueForKey(ent, "basetexture");
	if (basetexture_string)
	{
		pWaterControl->basetexture = basetexture_string;
		if (pWaterControl->basetexture[pWaterControl->basetexture.length() - 1] == '*')
		{
			pWaterControl->wildcard = pWaterControl->basetexture.substr(0, pWaterControl->basetexture.length() - 1);
		}
	}

	auto normalmap_string = ValueForKey(ent, "normalmap");

	if (normalmap_string)
	{
		pWaterControl->normalmap = normalmap_string;
	}

#define PARSE_KEY_VALUE_STRING(name, parser) auto name##_string = ValueForKey(ent, #name);\
	if (name##_string)\
	{\
		if (parser(name##_string, pWaterControl->name))\
		{\
		}\
		else\
		{\
			gEngfuncs.Con_Printf("R_LoadBSPEntities: Failed to parse \" #name \" in entity \"env_water_control\"\n");\
		}\
	}

#define PARSE_KEY_VALUE_STRING_WRITEREF(name, parser) auto name##_string = ValueForKey(ent, #name);\
	if (name##_string)\
	{\
		if (parser(name##_string, &pWaterControl->name))\
		{\
		}\
		else\
		{\
			gEngfuncs.Con_Printf("R_LoadBSPEntities: Failed to parse \" #name \" in entity \"env_water_control\"\n");\
		}\
	}

	PARSE_KEY_VALUE_STRING(fresnelfactor, UTIL_ParseStringAsVector4);
	PARSE_KEY_VALUE_STRING(depthfactor, UTIL_ParseStringAsVector3);
	PARSE_KEY_VALUE_STRING_WRITEREF(normfactor, UTIL_ParseStringAsVector1);
	PARSE_KEY_VALUE_STRING_WRITEREF(minheight, UTIL_ParseStringAsVector1);
	PARSE_KEY_VALUE_STRING_WRITEREF(maxtrans, UTIL_ParseStringAsVector1);
	PARSE_KEY_VALUE_STRING_WRITEREF(speedrate, UTIL_ParseStringAsVector1);

#undef PARSE_KEY_VALUE_STRING
#undef PARSE_KEY_VALUE_STRING_WRITEREF

	auto level_string = ValueForKey(ent, "level");
	if (level_string)
	{
		if (!strcmp(level_string, "WATER_LEVEL_LEGACY"))
		{
			pWaterControl->level = WATER_LEVEL_LEGACY;
		}
		else if (!strcmp(level_string, "WATER_LEVEL_REFLECT_SKYBOX"))
		{
			pWaterControl->level = WATER_LEVEL_REFLECT_SKYBOX;
		}
		else if (!strcmp(level_string, "WATER_LEVEL_REFLECT_WORLD"))
		{
			pWaterControl->level = WATER_LEVEL_REFLECT_WORLD;
		}
		else if (!strcmp(level_string, "WATER_LEVEL_REFLECT_ENTITY"))
		{
			pWaterControl->level = WATER_LEVEL_REFLECT_ENTITY;
		}
		else if (!strcmp(level_string, "WATER_LEVEL_REFLECT_SSR"))
		{
			pWaterControl->level = WATER_LEVEL_REFLECT_SSR;
		}
		else if (!strcmp(level_string, "WATER_LEVEL_LEGACY_RIPPLE"))
		{
			pWaterControl->level = WATER_LEVEL_LEGACY_RIPPLE;
		}
		else
		{
			int lv = 0;
			if (sscanf(level_string, "%d", &lv) == 1)
			{
				pWaterControl->level = math_clamp(lv, WATER_LEVEL_LEGACY, WATER_LEVEL_MAX - 1);
			}
			else
			{
				gEngfuncs.Con_Printf("R_LoadBSPEntities: Failed to parse \"level\" in entity \"env_water_control\", 1 integer is required\n");
			}
		}
	}

	if (pWaterControl->basetexture.length())
	{
		g_EnvWaterControls.emplace_back(pWaterControl);
	}
}

std::shared_ptr<CWorldSurfaceShadowProxyModel> R_LoadWorldSurfaceShadowProxyModel(const char* resourcePath);

void R_ParseBSPEntity_Env_Shadow_Proxy(bspentity_t* ent)
{
	/*
		// Loading "maps/de_dust2_shadow.obj" as shadow proxy geometry
		{
			"classname" "env_shadow_proxy"
			"model" "maps/de_dust2.bsp"
			"objpath" "maps/de_dust2_shadow.obj"
		}
	*/
	std::string model = ValueForKey(ent, "model");

	if (model.empty())
	{
		gEngfuncs.Con_Printf("R_LoadBSPEntities: Failed to parse \"model\" in entity \"env_shadow_proxy\"\n"); 
		return;
	}

	auto objpath_string = ValueForKey(ent, "objpath");

	if (objpath_string)
	{
		g_WorldSurfaceShadowProxyModels[model] = R_LoadWorldSurfaceShadowProxyModel(objpath_string);
	}
	else
	{
		gEngfuncs.Con_Printf("R_LoadBSPEntities: Failed to parse \"objpath\" in entity \"env_shadow_proxy\"\n");
		return;
	}
}

void R_ParseBSPEntity_Env_DynamicLight_Control(bspentity_t* ent)
{
	R_ParseMapCvarSetMapValue(r_dynlight_ambient, ValueForKey(ent, "ambient"));
	R_ParseMapCvarSetMapValue(r_dynlight_diffuse, ValueForKey(ent, "diffuse"));
	R_ParseMapCvarSetMapValue(r_dynlight_specular, ValueForKey(ent, "specular"));
	R_ParseMapCvarSetMapValue(r_dynlight_specularpow, ValueForKey(ent, "specularpow"));
}

void R_ParseBSPEntity_Env_FlashLight_Control(bspentity_t* ent)
{
	R_ParseMapCvarSetMapValue(r_flashlight_enable, ValueForKey(ent, "enable"));
	R_ParseMapCvarSetMapValue(r_flashlight_ambient, ValueForKey(ent, "ambient"));
	R_ParseMapCvarSetMapValue(r_flashlight_diffuse, ValueForKey(ent, "diffuse"));
	R_ParseMapCvarSetMapValue(r_flashlight_specular, ValueForKey(ent, "specular"));
	R_ParseMapCvarSetMapValue(r_flashlight_specularpow, ValueForKey(ent, "specularpow"));
	R_ParseMapCvarSetMapValue(r_flashlight_attachment, ValueForKey(ent, "attachment"));
	R_ParseMapCvarSetMapValue(r_flashlight_distance, ValueForKey(ent, "distance"));
	R_ParseMapCvarSetMapValue(r_flashlight_cone_cosine, ValueForKey(ent, "cone_cosine"));

	auto cone_texture = ValueForKey(ent, "cone_texture");

	if (cone_texture)
	{
		r_flashlight_cone_texture_name = cone_texture;
	}
}

void R_ParseBSPEntity_Env_HDR_Control(bspentity_t* ent)
{
	R_ParseMapCvarSetMapValue(r_hdr_adaptation, ValueForKey(ent, "adaptation"));
	R_ParseMapCvarSetMapValue(r_hdr_blurwidth, ValueForKey(ent, "blurwidth"));
	R_ParseMapCvarSetMapValue(r_hdr_darkness, ValueForKey(ent, "darkness"));
	R_ParseMapCvarSetMapValue(r_hdr_exposure, ValueForKey(ent, "exposure"));
}

void R_ParseBSPEntity_Env_Shadow_Control(bspentity_t* ent)
{
	
}

void R_ParseBSPEntity_Env_DeferredLighting_Control(bspentity_t* ent)
{
	R_ParseMapCvarSetMapValue(r_deferred_lightmap_pow, ValueForKey(ent, "lightmap_pow"));
	R_ParseMapCvarSetMapValue(r_deferred_lightmap_scale, ValueForKey(ent, "lightmap_scale"));
}

void R_ParseBSPEntity_Env_SSR_Control(bspentity_t* ent)
{
	R_ParseMapCvarSetMapValue(r_ssr_ray_step, ValueForKey(ent, "ray_step"));
	R_ParseMapCvarSetMapValue(r_ssr_iter_count, ValueForKey(ent, "iter_count"));
	R_ParseMapCvarSetMapValue(r_ssr_distance_bias, ValueForKey(ent, "distance_bias"));
	R_ParseMapCvarSetMapValue(r_ssr_exponential_step, ValueForKey(ent, "exponential_step"));
	R_ParseMapCvarSetMapValue(r_ssr_adaptive_step, ValueForKey(ent, "adaptive_step"));
	R_ParseMapCvarSetMapValue(r_ssr_binary_search, ValueForKey(ent, "binary_search"));
	R_ParseMapCvarSetMapValue(r_ssr_fade, ValueForKey(ent, "fade"));
}

void R_LoadBSPEntities(std::vector<bspentity_t*>& vBSPEntities)
{
	for (auto ent : vBSPEntities)
	{
		auto classname = ent->classname;

		if (!classname)
			continue;
#if 0
		if (!strcmp(classname, "env_cubemap"))
		{
			R_ParseBSPEntity_Env_Cubemap(ent);
		}
#endif	
		else if (!strcmp(classname, "light_dynamic"))
		{
			R_ParseBSPEntity_Light_Dynamic(ent);
		}

		else if (!strcmp(classname, "env_shadow_proxy"))
		{
			R_ParseBSPEntity_Env_Shadow_Proxy(ent);
		}

		else if (!strcmp(classname, "env_dynamiclight_control"))
		{
			R_ParseBSPEntity_Env_DynamicLight_Control(ent);
		}

		else if (!strcmp(classname, "env_flashlight_control"))
		{
			R_ParseBSPEntity_Env_FlashLight_Control(ent);
		}

		else if (!strcmp(classname, "env_water_control"))
		{
			R_ParseBSPEntity_Env_Water_Control(ent);
		}

		else if (!strcmp(classname, "env_hdr_control"))
		{
			R_ParseBSPEntity_Env_HDR_Control(ent);
		}

		else if (!strcmp(classname, "env_shadow_control"))
		{
			R_ParseBSPEntity_Env_Shadow_Control(ent);
		}

		else if (!strcmp(classname, "env_deferredlighting_control"))
		{
			R_ParseBSPEntity_Env_DeferredLighting_Control(ent);
		}

		else if (!strcmp(classname, "env_ssr_control"))
		{
			R_ParseBSPEntity_Env_SSR_Control(ent);
		}

	}//end for
}

void R_DrawBrushModel(cl_entity_t* e)
{
	qboolean rotated;

	(*currententity) = e;
	//(*currenttexture) = -1;

	auto clmodel = e->model;

	vec3_t entity_mins, entity_maxs;

	if (e->angles[0] || e->angles[1] || e->angles[2])
	{
		rotated = true;

		for (int i = 0; i < 3; i++)
		{
			entity_mins[i] = e->origin[i] - clmodel->radius;
			entity_maxs[i] = e->origin[i] + clmodel->radius;
		}
	}
	else
	{
		rotated = false;

		VectorAdd(e->origin, clmodel->mins, entity_mins);
		VectorAdd(e->origin, clmodel->maxs, entity_maxs);
	}

	if (R_CullBox(entity_mins, entity_maxs))
		return;

	VectorSubtract((*r_refdef.vieworg), e->origin, modelorg);

	if (rotated)
	{
		vec3_t temp;
		vec3_t forward, right, up;

		VectorCopy(modelorg, temp);
		AngleVectors(e->angles, forward, right, up);
		modelorg[0] = DotProduct(temp, forward);
		modelorg[1] = -DotProduct(temp, right);
		modelorg[2] = DotProduct(temp, up);
	}

	R_RotateForEntity(e, r_entity_matrix);

	R_SetGBufferMask(GBUFFER_MASK_ALL);

	R_SetRenderMode(e);

	if ((*currententity)->curstate.rendermode == kRenderTransColor)
	{
		g_WorldSurfaceRenderer.bDiffuseTexture = false;
		g_WorldSurfaceRenderer.bLightmapTexture = false;
	}
	else if ((*currententity)->curstate.rendermode == kRenderTransAlpha || (*currententity)->curstate.rendermode == kRenderNormal)
	{
		g_WorldSurfaceRenderer.bDiffuseTexture = true;
		g_WorldSurfaceRenderer.bLightmapTexture = true;
	}
	else
	{
		g_WorldSurfaceRenderer.bDiffuseTexture = true;
		g_WorldSurfaceRenderer.bLightmapTexture = false;
	}

	auto pModel = R_GetWorldSurfaceModel(clmodel);

	if (pModel)
	{
		R_DrawWorldSurfaceModel(pModel, e);
	}

	glDepthMask(GL_TRUE);
	glDisable(GL_BLEND);
}

void R_SetupCameraView(camera_view_t *view)
{
	InvertMatrix(r_world_matrix, r_world_matrix_inv);
	InvertMatrix(r_projection_matrix, r_projection_matrix_inv);

	memcpy(view->worldMatrix, r_world_matrix, sizeof(mat4));
	memcpy(view->projMatrix, r_projection_matrix, sizeof(mat4));
	memcpy(view->invWorldMatrix, r_world_matrix_inv, sizeof(mat4));
	memcpy(view->invProjMatrix, r_projection_matrix_inv, sizeof(mat4));
	memcpy(view->viewport, r_viewport, sizeof(float[4]));
	memcpy(view->frustum[0], r_frustum_origin[0], sizeof(vec3_t));
	memcpy(view->frustum[1], r_frustum_origin[1], sizeof(vec3_t));
	memcpy(view->frustum[2], r_frustum_origin[2], sizeof(vec3_t));
	memcpy(view->frustum[3], r_frustum_origin[3], sizeof(vec3_t));
	memcpy(view->viewpos, (*r_refdef.vieworg), sizeof(vec3_t));
	memcpy(view->vpn, vpn, sizeof(vec3_t));
	memcpy(view->vright, vright, sizeof(vec3_t));
	memcpy(view->vup, vup, sizeof(vec3_t));
}

void R_UploadCameraUBO()
{
	if (r_draw_multiview)
		return;

	camera_ubo_t CameraUBO{};

	R_SetupCameraView(&CameraUBO.views[0]);

	CameraUBO.numViews = 1;

	GL_UploadSubDataToUBO(g_WorldSurfaceRenderer.hCameraUBO, 0, sizeof(CameraUBO), &CameraUBO);
}

void R_UploadSceneUBO(void)
{
	scene_ubo_t SceneUBO;

	//normal[0] * x+ normal[1] * y+ normal[2] * z = normal[0] * vert[0] +normal[1] * vert[1] +normal[2] * vert[2]

	if (R_IsRenderingReflectView())
	{
		float equation[4] = { g_CurrentReflectCache->normal[0], g_CurrentReflectCache->normal[1], g_CurrentReflectCache->normal[2], -g_CurrentReflectCache->planedist };
		memcpy(SceneUBO.clipPlane, equation, sizeof(vec4_t));
	}
	else if (R_IsRenderingRefractView())
	{
		if (R_IsAboveWater(g_CurrentReflectCache))
		{
			if (g_CurrentReflectCache->normal[2] > 0)
			{
				float equation[4] = { g_CurrentReflectCache->normal[0], g_CurrentReflectCache->normal[1], -g_CurrentReflectCache->normal[2], g_CurrentReflectCache->planedist };
				memcpy(SceneUBO.clipPlane, equation, sizeof(vec4_t));
			}
			else
			{
				float equation[4] = { g_CurrentReflectCache->normal[0], g_CurrentReflectCache->normal[1], g_CurrentReflectCache->normal[2], g_CurrentReflectCache->planedist };
				memcpy(SceneUBO.clipPlane, equation, sizeof(vec4_t));
			}
		}
		else
		{
			float equation[4] = { g_CurrentReflectCache->normal[0], g_CurrentReflectCache->normal[1], -g_CurrentReflectCache->normal[2], g_CurrentReflectCache->planedist };
			memcpy(SceneUBO.clipPlane, equation, sizeof(vec4_t));
		}
	}
	else if (g_bPortalClipPlaneEnabled[0])
	{
		memcpy(SceneUBO.clipPlane, g_PortalClipPlane[0], sizeof(vec4_t));
	}
	else
	{
		memset(SceneUBO.clipPlane, 0, sizeof(vec4_t));
	}

	//Fog colors are converted to linear space before use.
	memcpy(SceneUBO.fogColor, r_fog_color, sizeof(vec4_t));

	SceneUBO.fogStart = r_fog_control[0];
	SceneUBO.fogEnd = r_fog_control[1];
	SceneUBO.fogDensity = r_fog_control[2];
	SceneUBO.cl_time = (*cl_time);

	float r_g = 1.0f / v_gamma->value;

	float r_g3;
	if (v_brightness->value <= 0.0f)
		r_g3 = 0.125f;
	else if (v_brightness->value > 1.0f)
		r_g3 = 0.05f;
	else
		r_g3 = 0.125f - (v_brightness->value * v_brightness->value) * 0.075f;

	SceneUBO.r_g = r_g;
	SceneUBO.r_g3 = r_g3;
	SceneUBO.v_brightness = v_brightness->value;
	SceneUBO.v_lightgamma = v_lightgamma->value;
	SceneUBO.v_lambert = v_lambert->value;
	SceneUBO.v_gamma = v_gamma->value;
	SceneUBO.v_texgamma = v_texgamma->value;
	SceneUBO.z_near = r_znear;
	SceneUBO.z_far = r_zfar;
	SceneUBO.r_alphamin = gl_alphamin->value;
	SceneUBO.r_linear_blend_shift = math_clamp(r_linear_blend_shift->value, 0, 1);
	SceneUBO.r_linear_fog_shift = math_clamp(r_linear_fog_shift->value, 0, 1);
	SceneUBO.r_linear_fog_shiftz = math_clamp(r_linear_fog_shiftz->value, 0, 1);

	if (R_IsRenderingDeferredLightingScene())
	{
		SceneUBO.r_lightmap_pow = r_deferred_lightmap_pow->GetValue();
		SceneUBO.r_lightmap_scale = r_deferred_lightmap_scale->GetValue();
	}
	else
	{
		SceneUBO.r_lightmap_pow = 1;
		SceneUBO.r_lightmap_scale = 1;
	}

	if (gl_overbright->value)
		SceneUBO.r_lightscale = 1;
	else
		SceneUBO.r_lightscale = ((pow(2.0f, 1.0f / v_lightgamma->value) * 256) + 0.5) / 256.0f;

	SceneUBO.r_filtercolor[0] = *filterColorRed;
	SceneUBO.r_filtercolor[1] = *filterColorGreen;
	SceneUBO.r_filtercolor[2] = *filterColorBlue;
	SceneUBO.r_filtercolor[3] = *filterBrightness;

	//Use vec4[256/4] instead of float[256] to save vram, float[256] in std140 costs 16 * 256 instead of 4 * 256 bytes due to alignment
	for (int i = 0; i < 256; ++i)
	{
		SceneUBO.r_lightstylevalue[i / 4][i % 4] = d_lightstylevalue[i] * (1.0f / 264.0f);
	}

	GL_UploadSubDataToUBO(g_WorldSurfaceRenderer.hSceneUBO, 0, sizeof(SceneUBO), &SceneUBO);
}

void R_UploadDLightUBO(void)
{
	dlight_ubo_t DLightUBO = { 0 };

	g_WorldSurfaceRenderer.iNumLegacyDLights = 0;

	if (!R_CanRenderGBuffer())
	{
		const auto PointLightCallback = [](PointLightCallbackArgs* args, void* context)
		{
			auto DLightUBO = (dlight_ubo_t*)(context);

			DLightUBO->origin_radius[g_WorldSurfaceRenderer.iNumLegacyDLights][0] = args->origin[0];
			DLightUBO->origin_radius[g_WorldSurfaceRenderer.iNumLegacyDLights][1] = args->origin[1];
			DLightUBO->origin_radius[g_WorldSurfaceRenderer.iNumLegacyDLights][2] = args->origin[2];
			DLightUBO->origin_radius[g_WorldSurfaceRenderer.iNumLegacyDLights][3] = args->radius;

			DLightUBO->color_minlight[g_WorldSurfaceRenderer.iNumLegacyDLights][0] = args->color[0];
			DLightUBO->color_minlight[g_WorldSurfaceRenderer.iNumLegacyDLights][1] = args->color[1];
			DLightUBO->color_minlight[g_WorldSurfaceRenderer.iNumLegacyDLights][2] = args->color[2];

			g_WorldSurfaceRenderer.iNumLegacyDLights++;
		};

		const auto SpotLightCallback = [](SpotLightCallbackArgs* args, void* context)
		{
			auto DLightUBO = (dlight_ubo_t*)(context);
			//Pass nothing to dlight ubo
		};

		const auto DirectionalLightCallback = [](DirectionalLightCallbackArgs* args, void* context)
		{
			auto DLightUBO = (dlight_ubo_t*)(context);
			//Pass nothing to dlight ubo
		};

		R_IterateDynamicLights(PointLightCallback, SpotLightCallback, DirectionalLightCallback, &DLightUBO);
	}

	DLightUBO.active_dlights[0] = g_WorldSurfaceRenderer.iNumLegacyDLights;

	GL_UploadSubDataToUBO(g_WorldSurfaceRenderer.hDLightUBO, 0, sizeof(DLightUBO), &DLightUBO);
}

/*
	Purpose : Setup texture states and SceneUBO for DrawWorld
*/
void R_PrepareDrawWorld(void)
{
	g_WorldSurfaceRenderer.bDiffuseTexture = true;
	g_WorldSurfaceRenderer.bLightmapTexture = false;

	R_UploadCameraUBO();
	R_UploadSceneUBO();
	R_UploadDLightUBO();
}

void R_DrawWorld(void)
{
	r_draw_opaque = true;

	const float r_identity_matrix[4][4] = {
		{1.0f, 0.0f, 0.0f, 0.0f},
		{0.0f, 1.0f, 0.0f, 0.0f},
		{0.0f, 0.0f, 1.0f, 0.0f},
		{0.0f, 0.0f, 0.0f, 1.0f}
	};

	memcpy(r_entity_matrix, r_identity_matrix, sizeof(r_entity_matrix));

	r_entity_color[0] = 1;
	r_entity_color[1] = 1;
	r_entity_color[2] = 1;
	r_entity_color[3] = 1;

	// 1:1 copy from R_DrawWorld, but with hw.dll!r_worldentity instead of stack entity.

	VectorCopy((*r_refdef.vieworg), modelorg);

	(*currententity) = r_worldentity;

	r_worldentity->curstate.rendercolor.r = gWaterColor->r;
	r_worldentity->curstate.rendercolor.g = gWaterColor->g;
	r_worldentity->curstate.rendercolor.b = gWaterColor->b;

	g_WorldSurfaceRenderer.bDiffuseTexture = true;
	g_WorldSurfaceRenderer.bLightmapTexture = true;

	auto pModel = R_GetWorldSurfaceModel((*cl_worldmodel));

	if (pModel)
	{
		R_DrawWorldSurfaceModel(pModel, (*currententity));
	}
}

#include "tiny_obj_loader.h"

#include <istream>
#include <streambuf>
#include <vector>
#include <cstring>

extern IFileSystem* g_pFileSystem;
extern IFileSystem_HL25* g_pFileSystem_HL25;

class CFileStreamBuffer : public std::streambuf {
public:
	CFileStreamBuffer(const std::string& filename) {

		auto hFileHandle = FILESYSTEM_ANY_OPEN(filename.c_str(), "rb");

		if (!hFileHandle) {
			throw std::runtime_error("Failed to open file: " + filename);
		}

		size_t fileSize = FILESYSTEM_ANY_SIZE(hFileHandle);
		buffer_.resize(fileSize);

		FILESYSTEM_ANY_READ(buffer_.data(), fileSize, hFileHandle);
		FILESYSTEM_ANY_CLOSE(hFileHandle);

		setg(buffer_.data(), buffer_.data(), buffer_.data() + buffer_.size());
	}

private:
	std::vector<char> buffer_;
};

class CFileSystemStream : public std::istream {
public:
	CFileSystemStream(const std::string& filename) : std::istream(&fileStreamBuffer_), fileStreamBuffer_(filename) {}

private:
	CFileStreamBuffer fileStreamBuffer_;
};

std::shared_ptr<CWorldSurfaceShadowProxyModel> R_LoadWorldSurfaceShadowProxyModel(const char * resourcePath)
{
	
	tinyobj::attrib_t attrib;
	std::vector<tinyobj::shape_t> shapes;
	std::vector<tinyobj::material_t> materials;
	std::string warn, err;

	CFileSystemStream fileStream(resourcePath);

	bool ret = tinyobj::LoadObj(
		&attrib,
		&shapes,
		&materials,
		&warn,
		&err,
		&fileStream,
		nullptr,
		true,
		false
	);

	if (!warn.empty()) {
		gEngfuncs.Con_DPrintf("R_LoadWorldSurfaceShadowProxyModel: (warning) %s.\n", err.c_str());
	}
	if (!err.empty()) {
		gEngfuncs.Con_DPrintf("R_LoadWorldSurfaceShadowProxyModel: (error) %s.\n", err.c_str());
	}
	if (!ret) {
		gEngfuncs.Con_DPrintf("R_LoadWorldSurfaceShadowProxyModel: Failed to load \"%s\".\n", resourcePath);
		return nullptr;
	}

	std::shared_ptr<CWorldSurfaceShadowProxyModel> pShadowProxyModel = std::make_shared<CWorldSurfaceShadowProxyModel>();

	std::vector<brushvertex_t> vVertexDataBuffer;
	std::vector<brushvertextbn_t> vVertexTBNDataBuffer;
	std::vector<brushinstancedata_t> vInstanceDataBuffer;
	std::vector<uint32_t> vIndiceBuffer;
	std::vector<CDrawIndexAttrib> vDrawAttribBuffer;

	for (const auto& shape : shapes) {

		uint32_t instanceIndex = (uint32_t)vInstanceDataBuffer.size();

		brushinstancedata_t instanceData;

		instanceData.lightmaptexturenum_texcoordscale[0] = 0;
		instanceData.lightmaptexturenum_texcoordscale[1] = 0;

		instanceData.styles[0] = 0;
		instanceData.styles[1] = 0;
		instanceData.styles[2] = 0;
		instanceData.styles[3] = 0;

		instanceData.matId = 0;

		vInstanceDataBuffer.emplace_back(instanceData);

		uint32_t baseVertex = (uint32_t)vVertexDataBuffer.size();
		uint32_t baseIndex = (uint32_t)vIndiceBuffer.size();
		size_t indiceCount = shape.mesh.indices.size();

		for (size_t j = 0; j < indiceCount; j ++) {

			const auto& index = shape.mesh.indices[j];

			brushvertex_t tempVertexData;

			tempVertexData.pos[0] = attrib.vertices[3 * index.vertex_index + 0];
			tempVertexData.pos[1] = attrib.vertices[3 * index.vertex_index + 1];
			tempVertexData.pos[2] = attrib.vertices[3 * index.vertex_index + 2];

			tempVertexData.texcoord[0] = attrib.texcoords[2 * index.texcoord_index + 0];
			tempVertexData.texcoord[1] = attrib.texcoords[2 * index.texcoord_index + 1];

			brushvertextbn_t tempVertexTBNData;

			tempVertexTBNData.normal[0] = attrib.normals[3 * index.normal_index + 0];
			tempVertexTBNData.normal[1] = attrib.normals[3 * index.normal_index + 1];
			tempVertexTBNData.normal[2] = attrib.normals[3 * index.normal_index + 2];

			tempVertexTBNData.s_tangent[0] = 0;
			tempVertexTBNData.s_tangent[1] = 0;
			tempVertexTBNData.s_tangent[2] = 0;

			tempVertexTBNData.t_tangent[0] = 0;
			tempVertexTBNData.t_tangent[1] = 0;
			tempVertexTBNData.t_tangent[2] = 0;

			vVertexDataBuffer.emplace_back(tempVertexData);
			vVertexTBNDataBuffer.emplace_back(tempVertexTBNData);
			vIndiceBuffer.emplace_back(baseVertex + j);
		}

		uint32_t startOffset = sizeof(CDrawIndexAttrib) * vDrawAttribBuffer.size();
		uint32_t drawCount = 0;
		uint32_t numIndices = 0;

		for (size_t f = 0; f < shape.mesh.num_face_vertices.size(); f++) {

			int fv = shape.mesh.num_face_vertices[f];
		
			numIndices += fv;
		}

		CDrawIndexAttrib drawAttrib;

		drawAttrib.FirstIndexLocation = baseIndex;
		drawAttrib.NumIndices = numIndices;
		drawAttrib.FirstInstanceLocation = instanceIndex;
		drawAttrib.NumInstances = 1;
		drawAttrib.BaseVertex = 0;

		vDrawAttribBuffer.emplace_back(drawAttrib);
		drawCount++;

		auto pShadowProxyDraw = std::make_shared<CWorldSurfaceShadowProxyDraw>(shape.name);

		pShadowProxyDraw->startOffset = startOffset;
		pShadowProxyDraw->drawCount = drawCount;
		pShadowProxyDraw->polyCount = numIndices / 3;

		pShadowProxyModel->DrawList.emplace_back(pShadowProxyDraw);
	}

	pShadowProxyModel->hEBO = GL_GenBuffer();
	GL_UploadDataToEBOStaticDraw(pShadowProxyModel->hEBO, sizeof(uint32_t) * vIndiceBuffer.size(), vIndiceBuffer.data());

	pShadowProxyModel->hVBO[WSURF_VBO_VERTEX] = GL_GenBuffer();
	GL_UploadDataToVBOStaticDraw(pShadowProxyModel->hVBO[WSURF_VBO_VERTEX], sizeof(brushvertex_t) * vVertexDataBuffer.size(), vVertexDataBuffer.data());

	pShadowProxyModel->hVBO[WSURF_VBO_VERTEXTBN] = GL_GenBuffer();
	GL_UploadDataToVBOStaticDraw(pShadowProxyModel->hVBO[WSURF_VBO_VERTEXTBN], sizeof(brushvertextbn_t) * vVertexTBNDataBuffer.size(), vVertexTBNDataBuffer.data());

	pShadowProxyModel->hVBO[WSURF_VBO_INSTANCE] = GL_GenBuffer();
	GL_UploadDataToVBOStaticDraw(pShadowProxyModel->hVBO[WSURF_VBO_INSTANCE], sizeof(brushinstancedata_t) * vInstanceDataBuffer.size(), vInstanceDataBuffer.data());

	pShadowProxyModel->hVAO = GL_GenVAO();

	GL_BindStatesForVAO(pShadowProxyModel->hVAO, [pShadowProxyModel]() {

		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, pShadowProxyModel->hEBO);

		glBindBuffer(GL_ARRAY_BUFFER, pShadowProxyModel->hVBO[WSURF_VBO_VERTEX]);

		glEnableVertexAttribArray(WSURF_VA_POSITION);
		glEnableVertexAttribArray(WSURF_VA_TEXCOORD);
		glEnableVertexAttribArray(WSURF_VA_LIGHTMAP_TEXCOORD);

		glVertexAttribPointer(WSURF_VA_POSITION, 3, GL_FLOAT, false, sizeof(brushvertex_t), OFFSET(brushvertex_t, pos));
		glVertexAttribPointer(WSURF_VA_TEXCOORD, 2, GL_FLOAT, false, sizeof(brushvertex_t), OFFSET(brushvertex_t, texcoord));
		glVertexAttribPointer(WSURF_VA_LIGHTMAP_TEXCOORD, 2, GL_FLOAT, false, sizeof(brushvertex_t), OFFSET(brushvertex_t, lightmaptexcoord));

		glBindBuffer(GL_ARRAY_BUFFER, pShadowProxyModel->hVBO[WSURF_VBO_VERTEXTBN]);

		glEnableVertexAttribArray(WSURF_VA_NORMAL);
		glEnableVertexAttribArray(WSURF_VA_S_TANGENT);
		glEnableVertexAttribArray(WSURF_VA_T_TANGENT);

		glVertexAttribPointer(WSURF_VA_NORMAL, 3, GL_FLOAT, false, sizeof(brushvertextbn_t), OFFSET(brushvertextbn_t, normal));
		glVertexAttribPointer(WSURF_VA_S_TANGENT, 3, GL_FLOAT, false, sizeof(brushvertextbn_t), OFFSET(brushvertextbn_t, s_tangent));
		glVertexAttribPointer(WSURF_VA_T_TANGENT, 3, GL_FLOAT, false, sizeof(brushvertextbn_t), OFFSET(brushvertextbn_t, t_tangent));

		glBindBuffer(GL_ARRAY_BUFFER, pShadowProxyModel->hVBO[WSURF_VBO_INSTANCE]);

		glEnableVertexAttribArray(WSURF_VA_TEXTURENUM);
		glEnableVertexAttribArray(WSURF_VA_STYLES);
		glEnableVertexAttribArray(WSURF_VA_MATID);

		glVertexAttribPointer(WSURF_VA_TEXTURENUM, 2, GL_FLOAT, false, sizeof(brushinstancedata_t), OFFSET(brushinstancedata_t, lightmaptexturenum_texcoordscale));
		glVertexAttribDivisor(WSURF_VA_TEXTURENUM, 1);

		glVertexAttribIPointer(WSURF_VA_STYLES, 4, GL_UNSIGNED_BYTE, sizeof(brushinstancedata_t), OFFSET(brushinstancedata_t, styles));
		glVertexAttribDivisor(WSURF_VA_STYLES, 1);

		glVertexAttribIPointer(WSURF_VA_MATID, 1, GL_UNSIGNED_INT, sizeof(brushinstancedata_t), OFFSET(brushinstancedata_t, matId));
		glVertexAttribDivisor(WSURF_VA_MATID, 1);
		});

	pShadowProxyModel->hABO = GL_GenBuffer();
	GL_UploadDataToABOStaticDraw(pShadowProxyModel->hABO, sizeof(CDrawIndexAttrib) * vDrawAttribBuffer.size(), vDrawAttribBuffer.data());

	return pShadowProxyModel;
}