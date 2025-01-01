#pragma once
#include "CoreMinimal.h"
#include "DynamicUploadBuffer.h"
#include "IGameApp.h"
#include "RootSignature.h"
#include "PipelineState.h"
#include "Common/FrameDescriptorHeap.h"
#include "glTFCommon.h"
#include "GpuBuffer.h"
#include "TextureManager.h"
#include "Utilities/GameUtility.h"

#define SHADING_MODEL_METALLIC_ROUGHNESS

namespace glTF
{
	struct BaseMaterial;
	struct MeshBatch; 
	struct DrawObject;
	class IModelImporter;
	class glTFImporter;
}

namespace Math
{
	class Camera;
}

namespace MyDirectX
{
	class CameraController;
	class Texture;
	class ManagedTexture;
	
	class glTFViewer : public IGameApp
	{
		enum ERSId : uint8_t
		{
			Constants = 0,
			PerObject = 1,
			PerCamera = 2,
			PerMaterial = 3,
			Textures = 4,
			LightBuffer = 5,
			GIBuffer = 6,
		};
		
		struct UpdateInfo
		{
			uint64_t fenceValue {0};
			std::shared_ptr<GpuBuffer> buffer {nullptr};
			uint32_t index {0};
		};
		
	public:
		glTFViewer(HINSTANCE hInstance, const std::string &glTFFileName, const wchar_t* title = L"Hello, World!", 
			UINT width = SCR_WIDTH, UINT height = SCR_HEIGHT);

		static constexpr uint32_t kMaxDescriptorNum = 2048u;
		static constexpr uint32_t kMaxMaterialNum = 1024u;
		static constexpr uint32_t kMaxInstanceNum = 4096u;
		
		virtual void Update(float deltaTime) override;
		virtual void Render() override;

		bool LoadFile(const std::string &fileName);
		void AddDrawObject(const std::string &name, const std::shared_ptr<glTF::DrawObject> &drawObject);
	
	protected:
		virtual bool InitAssets() override;
		void UpdateMeshBuffers(const std::pair<std::string, glTF::MeshBatch*> &meshData);
		void UpdateMeshDescriptors(const std::string &objName,  const glTF::DrawObject &drawObject);
		void UpdateTextures(const std::string &name,  const std::vector<const ManagedTexture*> &textures);
		bool UpdateTexture(const ManagedTexture *texture);

	private:
		bool InitCustom() override;
		virtual void CleanCustom() override;

		void InitDefaultTextures();

		void RenderObjects(GraphicsContext& gfx, const Math::Matrix4 &viewProjMat, ObjectFilter filter = ObjectFilter::kAll);

		void ResetCamera(const std::optional<Math::Vector3> &position, const std::optional<Math::AffineTransform> &transform);
		void ResetCamera(const Math::Camera &camera);

		std::string m_CameraName;
		std::unique_ptr<Math::Camera> m_Camera;
		std::unique_ptr<CameraController> m_CameraController;
		Math::Matrix4 m_ViewProjMatrix;

		GraphicsPSO m_ModelViewerPSO;

		/// Scene info
		std::unique_ptr<glTF::glTFImporter> m_Importer;
		glTF::BoundingBox m_SceneBoundingBox;

		// Mesh draw commands
		std::vector<std::shared_ptr<glTF::DrawObject>> m_DrawObjects;
		// [Name, index]
		std::map<std::string, uint32_t> m_NameAndObjIndexMap;

		// [Name, descriptorStartIndex]
		std::map<std::string, uint32_t> m_NameAndDescStartMap;
		// [Texture*, descriptorIndex]
		std::map<const Texture*, uint32_t> m_TextureIndexMap;

		uint32_t m_DefaultTextureDescIndex{0};

		DynamicUploadBuffer m_ViewBuffer;
		DynamicUploadBuffer m_MaterialBuffer;
		DynamicUploadBuffer m_InstanceBuffer;
		
		// TODO: needn't update these Buffers every frame
		
		// Descriptor heap
		FrameDescriptorHeap m_FrameDescriptorHeap{ D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, kMaxDescriptorNum };

		// Lights
		StructuredBuffer m_LightBuffer;
		uint32_t m_LightBufferDescIndex{0};

		// Mesh buffers
		StructuredBuffer m_GlobalVertexBuffer;
		ByteAddressBuffer m_GlobalIndexBuffer;
		std::vector<std::shared_ptr<StructuredBuffer>> m_VertexBuffers;
		std::vector<std::shared_ptr<ByteAddressBuffer>> m_IndexBuffers;
		std::map<std::string, uint32_t> m_MeshNameAndIndex;
		std::deque<UpdateInfo> m_UpdateVertexBuffers;
		std::deque<UpdateInfo> m_UpdateIndexBuffers;
		// Use one global vertex buffer or one vertex buffer per mesh ?
		bool m_bUseGlobalMeshBuffers = false;

		// SH
		RootSignature m_SHRS;
		ComputePSO m_SHPSO;
		D3D12_CPU_DESCRIPTOR_HANDLE m_SHsrv{};
		StructuredBuffer m_SHOutput;
		uint32_t m_SHBufferDescIndex{0};

		std::vector<std::string> m_FileNames; 
	};
}
