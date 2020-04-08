#pragma once
#include "pch.h"
#include "Math/GLMath.h"
#include "GpuBuffer.h"
#include "DynamicUploadBuffer.h"
#include "Scenes/VertexLayout.h"
#include "Scenes/Material.h"
#include "Camera.h"
#include "CameraController.h"
#include "RootSignature.h"
#include "PipelineState.h"

namespace MyDirectX
{
	class GameInput;
	class GraphicsContext;
	class ComputeContext;
}

namespace MFalcor
{
	using namespace MyDirectX;

	struct Node
	{
		Node() = default;
		std::string name;
		uint32_t parentIndex = -1;
		
		Matrix4x4 transform;	// the node's transform matrix
		Matrix4x4 localToBindSpace;	// local to bind space transformation
	};

	struct MeshDesc
	{
		uint32_t vertexOffset = 0;
		uint32_t vertexByteSize = 0;
		uint32_t vertexStrideSize = 0;
		uint32_t vertexCount = 0;

		uint32_t indexByteOffset = 0;
		uint32_t indexByteSize = 0;
		uint32_t indexStrideSize = 16;	// uint16_t
		uint32_t indexCount = 0;

		uint32_t materialID = 0;
	};

	struct MeshInstanceData
	{
		uint32_t globalMatrixID = 0;
		uint32_t materialID = 0;
		uint32_t meshID = 0;
		uint32_t flags = 0;		// MeshInstanceFlags
	};

	class Scene
	{
	public:
		friend class AssimpImporter;

		using SharedPtr = std::shared_ptr<Scene>;
		using ConstSharedPtrRef = const SharedPtr&;

		static const uint32_t kMaxBonesPerVertex = 4;
		static const uint32_t kInvalidNode = -1;

		// flags indicating if and what was updated in the scene
		enum class UpdateFlags
		{
			None = 0x0,	// nothing happened
			MeshesMoved = 0x1,	// meshes moved
			CameraMoved = 0x2,	// the camera moved
			CameraPropsChanged = 0x4,	// some camera properties changed, excluding position
			LightsMoved = 0x8,	// lights were moved
			LightIntensityChanged = 0x10,	// light intensity changed
			LightPropsChanged = 0x20,	// other light changes not included in LightIntensityChanged and LightsMoved
			SceneGraphChanged = 0x40,	// any transform in the scene graph changed
			LightCollectionChanged = 0x80,	// light collection changed (mesh lights)
			MaterialsChanged = 0x100,

			All = -1
		};

		// settings for how the scene is updated
		enum class UpdateMode
		{
			Rebuild,	// recreate acceleration structure when updates are needed
			Refit		// update acceleration structure when updates are needed
		};

		static SharedPtr Create(ID3D12Device *pDevice, const std::string& filePath, GameInput* pInput = nullptr);
		static SharedPtr Create() { return SharedPtr(new Scene()); }

		Scene() = default;

		bool Init(ID3D12Device* pDevice, const std::string& filePath, GameInput* pInput = nullptr);

		// do any additional initialization required after scene data is set and draw lists are determined
		void Finalize();

		/// ** Framework ** 
		UpdateFlags Update(float deltaTime);

		// get the changes that happended during the last update
		// the flags only change during an `Update` call, if something changed between calling `Update` and `GetUpdates()`,
		// the returned result will not reflect it 
		UpdateFlags GetUpdates() const;

		// render the scene using the rasterizer
		void Render(GraphicsContext &gfx, AlphaMode alphaMode = AlphaMode::UNKNOWN);
		void BeginRendering(GraphicsContext& gfx);
		void SetRenderCamera(GraphicsContext& gfx, const Matrix4x4 &viewProjMat, const Vector3 &camPos);
		void RenderByAlphaMode(GraphicsContext& gfx, GraphicsPSO &pso, AlphaMode alphaMode);
		void RenderAlphaMask(GraphicsContext& gfx);
		void RenderTransparent(GraphicsContext& gfx);

		// render the scene using raytracing
		void Raytrace();

		// 
		void Clean();

		/// ** Camera **
		// access the scene's camera to change properties, or use elsewhere
		Math::Camera* GetCamera() const { return m_Camera.get(); }

		// attach a new camera to the scene
		void SetCamera();

		// set a camera controller type
		void SetCameraController();

		// get the camera controller type
		void GetCameraControllerType() const {}

		// toggle whether the camera is animated
		void ToggleCameraAnimation(bool bAnimated);

		// reset the camera
		// this function will place the camera at the center of scene and optionally set the depth range to some pre-determined values
		void ResetCamera(bool bResetDepthRange = false);

		// save the current camera viewport and returns a reference to it
		void SaveNewViewport();

		// remove the currently active viewport
		void RemoveViewport();

		// load a selected camera viewport and returns a reference to it
		void GotoViewport(uint32_t index);

		/// ** Mesh **
		// get the number of meshes
		uint32_t GetMeshCount() const { return (uint32_t)m_MeshDescs.size(); }

		// get a mesh desc
		const MeshDesc& GetMesh(uint32_t meshId) const { return m_MeshDescs[meshId]; }

		// get the number of mesh instances
		uint32_t GetMeshInstanceCount() const { return (uint32_t)m_MeshInstanceData.size(); }

		// get a mesh instance desc
		const MeshInstanceData& GetMeshInstance(uint32_t instanceId)  const
		{
			return m_MeshInstanceData[instanceId];
		}

		/// ** Material **
		// get the number of materials in the scene
		uint32_t GetMaterialCount() const { return(uint32_t)m_Materials.size(); }

		// get a material
		Material::ConstSharedPtrRef GetMaterial(uint32_t materialId) const
		{
			return m_Materials[materialId];
		}

		// get a material by name
		Material::SharedPtr GetMaterialByName(const std::string& name) const;

		/// ** Scene **
		// get the scene bounds
		const BoundingBox& GetSceneBounds() const { return m_SceneBB; }

		// get a mesh's bounds
		const BoundingBox& GetMeshBounds(uint32_t meshId) const { return m_MeshBBs[meshId]; }

		// ** Light **
		uint32_t GetLightCount() const;

		// get a light
		void GetLight(uint32_t lightId) const;

		// get a light by name
		void GetLightByName(const std::string& name) const;

		/**
			get the light collection representing all the mesh lights in the scene
		The light collection is created lazily on the first call. It needs a render context to run the init shaders.
		*/
		void GetLightCollection();

		// get the light probe or nullptr if it doesn't exist
		void GetLightProbe();

		// toggle whether the specified light is animated
		void ToggleLightAnimation(int index, bool bAnimated);

		// get/set how the scene's TLASes are updated when raytracing/
		// TLAS are REBUILT by default
		void SetTLASUpdateMode(UpdateMode mode);
		UpdateMode GetTLASUpdateMode();

		// get/set how the scene's BLASes are updated when raytracing
		// BLASes are REFIT by default
		void SetBLASUpdateMode(UpdateMode mode);
		UpdateMode GetBLASUpdateMode();

		/// settings
		// set an environment map
		void SetEnvironmentMap();

		// get the environment map
		void GetEnvironmentMap() const;

		/// ** Animaton **
		void GetAnimatonController() const;

		// toggle all animations on or off
		void ToggleAnimations(bool bAnimated);
	
	private:
		// create scene parameter block and retrieve pointers to buffers
		void InitResources();

		// uploads scene data to parameter block
		void UploadResources();

		// uploads a single material
		void UploadMaterial(uint32_t materialId);

		// -mf
		void UpdateMatrices();

		// update the scene's global bounding box
		void UpdateBounds();

		// create the draw list for rasterization
		void CreateDrawList();

		// sort what meshes go in what BLAS. 
		void SortBlasMeshes();

		// initialize geometry descs for each BLAS
		void InitGeoDescs();

		// generate bottom level acceleration structures for all meshes
		void BuildBLAS();

		// generate data for creating a TLAS
		void FillInstanceDesc(std::vector<D3D12_RAYTRACING_INSTANCE_DESC>& instanceDescs, uint32_t rayCount, bool perMeshHitEntry);

		// generate top level acceleration structure for the scene. Automatically determines whether to build or refit.
		// [in] rayCount - number of ray types in the shader. Required to setup how instances index into the Shader Table
		void BuildTLAS(uint32_t rayCount, bool perMeshEntry);

		// create the buffer that maps Acceleration Structure indices to their location in mMeshInstanceData
		// mMeshInstanceData should be indexed with [InstanceID() + GeometryIndex]
		void UpdateAsToInstanceDataMapping();

		UpdateFlags UpdateCamera(bool forceUpdate);
		UpdateFlags UpdataLights(bool forceUpdate);
		UpdateFlags UpdateMaterials(bool forceUpdate);

		void UpdateGeometryStats();

		struct GeometryStats
		{
			size_t uniqueTriangleCount = 0;		// number of unique triangles. A triangle can exist in multiple instances 
			size_t uniqueVertexCount = 0;		// number of unique vertices. A vertex can be referenced by multiple triangles/instances
			size_t instancedTriangleCount = 0;	// number of instanced triangles. This is the total number of rendered triangles
			size_t instancedVertexCount = 0;	// number of instanced vertices. This is the total number of vertices in the rendered triangles
		};

		// 
		// scene geometry
		VertexBufferLayout::SharedPtr m_VertexLayout;
		std::shared_ptr<StructuredBuffer> m_VertexBuffer;
		std::shared_ptr<ByteAddressBuffer> m_IndexBuffer;

		std::vector<MeshDesc> m_MeshDescs;
		std::vector<MeshInstanceData> m_MeshInstanceData;
		std::vector<Node> m_SceneGraph;		// for each index i, the array element indicates the parent node.
			// indices are in relation to m_LocalToWorldMatrices;
		// -mf matrices
		std::vector<Matrix4x4> m_LocalMatrices;
		std::vector<Matrix4x4> m_GlobalMatrices;
		std::vector<Matrix4x4> m_InvTransposeGlobalMatrices;

		// 
		std::vector<Material::SharedPtr> m_Materials;
		// LightCollecition
		// LightProbe
		// Texture envMap;

		// scene metadata (CPU only)
		std::vector<BoundingBox> m_MeshBBs;		// bounding boxes for meshes (not instances)
		std::vector<std::vector<uint32_t>> m_MeshIdToInstanceIds;	// mapping of what instances belong to which mesh
		BoundingBox m_SceneBB;	// bounding boxes of the entire scene
		std::vector<bool> m_MeshHasDynamicData;	// whether a mesh has dynamic data, meaning it is skinned
		GeometryStats m_GeometryStats;

		// resources
		DynamicUploadBuffer m_MaterialsBuffer;
		StructuredBuffer m_MeshInstancesBuffer;
		StructuredBuffer m_LightsBuffer;
		// ...

		// saved camera viewpoints
		struct Viewport
		{
			Vector3 position;
			Vector3 target;
			Vector3 up;
		};
		std::vector<Viewport> m_Viewports;
		uint32_t m_CurViewport = 0;
		// -mf
		std::shared_ptr<Math::Camera> m_Camera;
		std::shared_ptr<CameraController> m_CameraController;
		Math::Matrix4 m_ViewProjMatrix;
		GameInput* m_pInput = nullptr;

		// rendering 
		UpdateFlags m_UpdateFlag = UpdateFlags::All;

		// raytracing data
		UpdateMode m_TLASUpdateMode = UpdateMode::Rebuild;	// how the TLAS should be updated when there are changes in the scene
		UpdateMode m_BLASUpdateMode = UpdateMode::Refit;	// how the BLAS should be updated when there are changes to meshes
		std::vector<D3D12_RAYTRACING_INSTANCE_DESC> m_InstanceDescs;	// shared between TLAS builds to avoid reallocating CPU memory
		// ...

		std::string m_Name;

		// root signatures & PSOs
		RootSignature m_CommonRS;
		GraphicsPSO m_DebugWireframePSO;
		GraphicsPSO m_DepthPSO;
		GraphicsPSO m_DepthClipPSO;
		GraphicsPSO m_OpaqueModelPSO;
		GraphicsPSO m_MaskModelPSO;
		GraphicsPSO m_TransparentModelPSO;
	};

}
