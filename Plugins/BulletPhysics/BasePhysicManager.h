#pragma once

#include <vector>
#include <map>
#include <unordered_map>

#include "ClientPhysicManager.h"
#include "ClientPhysicConfig.h"

class CPhysicObjectCreationParameter
{
public:
	cl_entity_t* m_entity{};
	entity_state_t* m_entstate{};
	int m_entindex{};
	model_t* m_model{};
	studiohdr_t* m_studiohdr{};
	float m_model_scaling{ 1 };
	int m_playerindex{};
};

class CStaticObjectCreationParameter : public CPhysicObjectCreationParameter
{
public:
	CClientStaticObjectConfig* m_pStaticObjectConfig{};
};

class CDynamicObjectCreationParameter : public CPhysicObjectCreationParameter
{
public:
	bool m_allowNonNativeRigidBody{};
	CClientDynamicObjectConfig* m_pDynamicObjectConfig{};
};

class CRagdollObjectCreationParameter : public CPhysicObjectCreationParameter
{
public:
	bool m_allowNonNativeRigidBody{};
	CClientRagdollObjectConfig* m_pRagdollObjectConfig{};
};

class CBasePhysicAction : public IPhysicAction
{
public:
	CBasePhysicAction(IPhysicObject *pPhysicObject, int entindex, int flags) : m_pPhysicObject(pPhysicObject), m_entindex(entindex), m_flags(flags)
	{

	}

	void TransferOwnership(int entindex) override
	{
		m_entindex = entindex;
	}

	IPhysicObject* GetOwnerPhysicObject() const override
	{
		return m_pPhysicObject;
	}

	int GetOwnerEntityIndex() const override
	{
		return m_entindex;
	}

	int GetActionFlags() const override
	{
		return m_flags;
	}


	IPhysicObject* m_pPhysicObject{};
	int m_entindex{ };
	int m_flags{};
};

class CPhysicComponentAction : public CBasePhysicAction
{
public:
	CPhysicComponentAction(IPhysicObject* pPhysicObject, int entindex, int flags, int physicComponentId) : CBasePhysicAction(pPhysicObject, entindex, flags), m_physicComponentId(physicComponentId)
	{

	}

	int m_physicComponentId{};
};

class CBasePhysicRigidBody : public IPhysicRigidBody
{
public:
	CBasePhysicRigidBody(
		int id,
		int entindex, 
		IPhysicObject *pPhysicObject, 
		const CClientRigidBodyConfig* pRigidConfig);

	int GetPhysicConfigId() const override
	{
		return m_configId;
	}

	int GetPhysicComponentId() const override
	{
		return m_id;
	}

	int GetOwnerEntityIndex() const override
	{
		return m_entindex;
	}

	IPhysicObject* GetOwnerPhysicObject() const override
	{
		return m_pPhysicObject;
	}

	const char* GetName() const override
	{
		return m_name.c_str();
	}

	int GetFlags() const override
	{
		return m_flags;
	}

	int GetDebugDrawLevel() const override
	{
		return m_debugDrawLevel;
	}

	bool ShouldDrawOnDebugDraw(const CPhysicDebugDrawContext* ctx) const override
	{
		if (m_debugDrawLevel > 0 && ctx->m_ragdollObjectLevel > 0 && ctx->m_ragdollObjectLevel >= m_debugDrawLevel)
			return true;

		return false;
	}

	void TransferOwnership(int entindex) override
	{
		m_entindex = entindex;
	}

public:
	int m_id{};
	int m_entindex{};
	IPhysicObject* m_pPhysicObject{};
	std::string m_name;
	int m_flags{};
	int m_debugDrawLevel{ BULLET_DEFAULT_DEBUG_DRAW_LEVEL };
	int m_configId{};

	int m_boneindex{ -1 };
};

class CBasePhysicConstraint : public IPhysicConstraint
{
public:
	CBasePhysicConstraint(
		int id,
		int entindex,
		IPhysicObject* pPhysicObject,
		const CClientConstraintConfig* pConstraintConfig);

	int GetPhysicConfigId() const override
	{
		return m_configId;
	}

	int GetPhysicComponentId() const override
	{
		return m_id;
	}

	int GetOwnerEntityIndex() const override
	{
		return m_entindex;
	}

	IPhysicObject* GetOwnerPhysicObject() const override
	{
		return m_pPhysicObject;
	}

	const char* GetName() const override
	{
		return m_name.c_str();
	}

	int GetFlags() const override
	{
		return m_flags;
	}

	int GetDebugDrawLevel() const override
	{
		return m_debugDrawLevel;
	}

	bool ShouldDrawOnDebugDraw(const CPhysicDebugDrawContext* ctx) const override
	{
		if (m_debugDrawLevel > 0 && ctx->m_constraintLevel > 0 && ctx->m_constraintLevel >= m_debugDrawLevel)
			return true;

		return false;
	}

	void TransferOwnership(int entindex) override
	{
		m_entindex = entindex;
	}

public:
	int m_id{};
	int m_entindex{};
	IPhysicObject* m_pPhysicObject{};
	std::string m_name;
	int m_flags{};
	int m_debugDrawLevel{ BULLET_DEFAULT_DEBUG_DRAW_LEVEL };
	int m_configId{};
};

class CBasePhysicManager : public IClientPhysicManager
{
protected:
	//CPhysicIndexArray* m_barnacleIndexArray{};
	//CPhysicVertexArray* m_barnacleVertexArray{};
	//CPhysicIndexArray* m_gargantuaIndexArray{};
	//CPhysicVertexArray* m_gargantuaVertexArray{};

	float m_gravity{};

	uint64 m_inspectedPhysicObjectId{};
	int m_inspectedPhysicComponentId{};

	uint64 m_selectedPhysicObjectId{};
	int m_selectedPhysicComponentId{};

	int m_allocatedPhysicConfigId{};
	int m_allocatedPhysicComponentId{};

	std::unordered_map<int, IPhysicObject *> m_physicObjects;
	std::unordered_map<int, IPhysicComponent*> m_physicComponents;
	std::unordered_map<int, std::weak_ptr<CClientBasePhysicConfig>> m_physicConfigs;

	std::shared_ptr<CPhysicVertexArray> m_worldVertexArray;
	//std::vector<std::shared_ptr<CPhysicIndexArray>> m_brushIndexArray;

	CClientPhysicObjectConfigs m_physicObjectConfigs;

	std::unordered_map<std::string, std::shared_ptr<CPhysicIndexArray>> m_indexArrayResources;

	CPhysicDebugDrawContext m_debugDrawContext;
public:

	void Destroy(void) override;
	void Init(void) override;
	void Shutdown() override;
	void NewMap(void) override;
	void SetGravity(float value) override;
	void StepSimulation(double frametime) override;

	bool SetupBones(studiohdr_t* studiohdr, int entindex) override;
	bool SetupJiggleBones(studiohdr_t* studiohdr, int entindex) override;
	//bool MergeBones(studiohdr_t* studiohdr, int entindex) override;

	//PhysicObjectConfig Management
	bool SavePhysicObjectConfigForModel(model_t* mod) override;
	bool SavePhysicObjectConfigForModelIndex(int modelindex) override;
	std::shared_ptr<CClientPhysicObjectConfig> LoadPhysicObjectConfigForModel(model_t* mod) override;
	std::shared_ptr<CClientPhysicObjectConfig> CreateEmptyPhysicObjectConfigForModel(model_t* mod, int PhysicObjectType) override;
	std::shared_ptr<CClientPhysicObjectConfig> CreateEmptyPhysicObjectConfigForModelIndex(int modelindex, int PhysicObjectType) override;
	std::shared_ptr<CClientPhysicObjectConfig> GetPhysicObjectConfigForModel(model_t* mod) override;
	std::shared_ptr<CClientPhysicObjectConfig> GetPhysicObjectConfigForModelIndex(int modelindex);
	void LoadPhysicObjectConfigs(void) override; 
	void SavePhysicObjectConfigs(void) override;
	bool SavePhysicObjectConfigToFile(const std::string& filename, CClientPhysicObjectConfig* pPhysicObjectConfig) override;
	void RemoveAllPhysicObjectConfigs(int withflags, int withoutflags) override;

	//PhysicObject Management
	IPhysicObject* GetPhysicObject(int entindex) override;
	IPhysicObject* GetPhysicObjectEx(uint64 physicObjectId) override;
	void AddPhysicObject(int entindex, IPhysicObject* pPhysicObject) override; 
	void FreePhysicObject(IPhysicObject* pPhysicObject) override;
	bool RemovePhysicObject(int entindex) override;
	bool RemovePhysicObjectEx(uint64 physicObjectId) override;
	void RemoveAllPhysicObjects(int withflags, int withoutflags) override;
	bool TransferOwnershipForPhysicObject(int old_entindex, int new_entindex) override;
	bool RebuildPhysicObject(int entindex, const CClientPhysicObjectConfig* pPhysicObjectConfig) override;
	bool RebuildPhysicObjectEx(uint64 physicObjectId, const CClientPhysicObjectConfig* pPhysicObjectConfig);
	void UpdateAllPhysicObjects(TEMPENTITY** ppTempEntFree, TEMPENTITY** ppTempEntActive, double frame_time, double client_time) override;

	void CreatePhysicObjectForEntity(cl_entity_t* ent, entity_state_t* state, model_t *mod) override;
	
	void SetupBonesForRagdoll(cl_entity_t* ent, entity_state_t* state, model_t* mod, int entindex, int playerindex) override;
	void SetupBonesForRagdollEx(cl_entity_t* ent, entity_state_t* state, model_t* mod, int entindex, int playerindex, const CClientAnimControlConfig* pOverrideAnimControl) override;
	void UpdateBonesForRagdoll(cl_entity_t* ent, entity_state_t* state, model_t* mod, int entindex, int playerindex) override;
	
	IPhysicObject* FindBarnacleObjectForPlayer(entity_state_t* state) override;
	IPhysicObject* FindGargantuaObjectForPlayer(entity_state_t* playerState) override;

	//PhysicComponent Management
	int AllocatePhysicComponentId() override; 
	IPhysicComponent* GetPhysicComponent(int physicComponentId) override;
	void AddPhysicComponent(int physicComponentId, IPhysicComponent* pPhysicComponent) override;
	void FreePhysicComponent(IPhysicComponent* pPhysicComponent) override;
	bool RemovePhysicComponent(int physicComponentId) override;

	//Inspect / Select System

	void SetInspectedPhysicComponentId(int physicComponentId) override;
	int  GetInspectedPhysicComponentId() const override;

	void SetSelectedPhysicComponentId(int physicComponentId) override;
	int  GetSelectedPhysicComponentId() const override;

	void   SetInspectedPhysicObjectId(uint64 physicObjectId) override;
	uint64 GetInspectedPhysicObjectId() const override;

	void   SetSelectedPhysicObjectId(uint64 physicObjectId) override;
	uint64 GetSelectedPhysicObjectId() const override;

	const CPhysicDebugDrawContext* GetDebugDrawContext() const override;

	//BasePhysicConfig Management
	int AllocatePhysicConfigId() override;
	std::weak_ptr<CClientBasePhysicConfig> GetPhysicConfig(int configId) override;
	void AddPhysicConfig(int configId, const std::shared_ptr<CClientBasePhysicConfig>& pPhysicConfig) override;
	bool RemovePhysicConfig(int configId) override;
	void RemoveAllPhysicConfigs() override;

	//VertexIndexArray Management
	std::shared_ptr<CPhysicIndexArray> LoadIndexArrayFromResource(const std::string& resourcePath) override;
	void FreeAllIndexArrays(int withflags, int withoutflags) override;
	
public:

	virtual IStaticObject* CreateStaticObject(const CStaticObjectCreationParameter& CreationParam) = 0;
	virtual IDynamicObject* CreateDynamicObject(const CDynamicObjectCreationParameter& CreationParam) = 0;
	virtual IRagdollObject* CreateRagdollObject(const CRagdollObjectCreationParameter& CreationParam) = 0;

private:
	//WorldVertexArray and WorldIndexArray
	void GenerateWorldVertexArray();
	void GenerateBrushIndexArray();
	void GenerateIndexArrayForBrushModel(model_t* mod, CPhysicVertexArray* vertexArray, CPhysicIndexArray* indexArray);
	void GenerateIndexArrayRecursiveWorldNode(mnode_t* node, CPhysicVertexArray* vertexArray, CPhysicIndexArray* indexArray);
	void GenerateIndexArrayForSurface(msurface_t* psurf, CPhysicVertexArray* vertexarray, CPhysicIndexArray* indexarray);
	void GenerateIndexArrayForBrushface(CPhysicBrushFace* brushface, CPhysicIndexArray* indexArray);

	//Deprecated: use Resource Management now
#if 0
	//std::shared_ptr<CPhysicIndexArray> GetIndexArrayFromBrushModel(model_t* mod);

	//void GenerateBarnacleIndexVertexArray();
	//void FreeBarnacleIndexVertexArray();
	//void GenerateGargantuaIndexVertexArray();
	//void FreeGargantuaIndexVertexArray();
#endif
	void CreatePhysicObjectFromConfig(cl_entity_t* ent, entity_state_t* state, model_t* mod, int entindex, int playerindex);
	void CreatePhysicObjectForStudioModel(cl_entity_t* ent, entity_state_t* state, model_t* mod);
	void CreatePhysicObjectForBrushModel(cl_entity_t* ent, entity_state_t* state, model_t* mod);

	void LoadAdditionalResourcesForConfig(CClientPhysicObjectConfig* pPhysicObjectConfig);
	void LoadAdditionalResourcesForCollisionShapeConfig(CClientCollisionShapeConfig* pCollisionShapeConfig);

	bool CreateEmptyPhysicObjectConfig(const std::string& filename, CClientPhysicObjectConfigStorage& Storage, int PhysicObjectType);
	bool LoadPhysicObjectConfigFromFiles(const std::string& filename, CClientPhysicObjectConfigStorage& Storage);
	bool LoadPhysicObjectConfigFromBSP(model_t* mod, CClientPhysicObjectConfigStorage& Storage);
	void OverwritePhysicObjectConfig(const std::string& filename, CClientPhysicObjectConfigStorage& Storage, const std::shared_ptr<CClientPhysicObjectConfig>& pPhysicObjectConfig);

	bool LoadObjToPhysicArrays(const std::string& resourcePath, std::shared_ptr<CPhysicIndexArray>& pIndexArray);

};

bool CheckPhysicComponentFilters(IPhysicComponent* pPhysicComponent, const CPhysicComponentFilters& filters);

void FloatGoldSrcToBullet(float* v);
void FloatBulletToGoldSrc(float* v);
void Vec3GoldSrcToBullet(vec3_t vec);
void Vec3BulletToGoldSrc(vec3_t vec);