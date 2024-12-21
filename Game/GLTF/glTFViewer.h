#pragma once
#include "CoreMinimal.h"
#include "IGameApp.h"
#include "RootSignature.h"
#include "PipelineState.h"
#include "Common/FrameDescriptorHeap.h"
#include "glTFCommon.h"
#include "GpuBuffer.h"

#define SHADING_MODEL_METALLIC_ROUGHNESS

namespace glTF
{
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

		virtual void Update(float deltaTime) override;
		virtual void Render() override;

		bool LoadFile(const std::string &fileName);
		void AddDrawObject(const std::string &name, const std::shared_ptr<glTF::DrawObject> &drawObject);
	
	protected:
		virtual bool InitAssets() override;
		void UpdateMeshBuffers(const std::pair<std::string, glTF::MeshBatch*> &meshData);

	private:
		bool InitCustom() override;
		virtual void CleanCustom() override;

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
		std::map<std::string, uint32_t> m_NameAndObjIndexMap;
		
		// Descriptor heap
		FrameDescriptorHeap m_FrameDescriptorHeap;

		// lights
		StructuredBuffer m_LightBuffer;

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
		// resources
		D3D12_CPU_DESCRIPTOR_HANDLE m_SHsrv{};
		StructuredBuffer m_SHOutput;

		std::vector<std::string> m_FileNames; 
	};
}
