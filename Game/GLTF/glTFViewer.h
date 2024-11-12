#pragma once
#include "CoreMinimal.h"
#include "IGameApp.h"
#include "RootSignature.h"
#include "PipelineState.h"
#include "Camera.h"
#include "CameraController.h"
#include "FrameDescriptorHeap.h"
#include "glTFCommon.h"
#include "GpuBuffer.h"

#define SHADING_MODEL_METALLIC_ROUGHNESS

namespace glTF
{
	struct MeshBatch;
	class IModelImporter;
	class glTFImporterNew;
}

namespace MyDirectX
{
	class glTFViewer : public IGameApp
	{
	public:
		glTFViewer(HINSTANCE hInstance, const std::string &glTFFileName, const wchar_t* title = L"Hello, World!", 
			UINT width = SCR_WIDTH, UINT height = SCR_HEIGHT);

		virtual void Update(float deltaTime) override;
		virtual void Render() override;

		void UpdateMeshBuffers(const std::pair<std::string, glTF::MeshBatch> &meshData);

	protected:
		virtual bool InitAssets() override;

	private:
		bool InitCustom() override;
		virtual void CleanCustom() override;

		void RenderObjects(GraphicsContext& gfx, const Math::Matrix4 &viewProjMat, ObjectFilter filter = ObjectFilter::kAll);

		Math::Camera m_Camera;
		std::unique_ptr<CameraController> m_CameraController;
		Math::Matrix4 m_ViewProjMatrix;

		GraphicsPSO m_ModelViewerPSO;

		/// Scene info
		std::unique_ptr<glTF::glTFImporterNew> m_Importer;
		glTF::BoundingBox m_SceneBoundingBox;

		// Mesh draw commands
		// TODO...
		
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
		std::vector<std::shared_ptr<StructuredBuffer>> m_UpdateVertexBuffers;
		std::vector<std::shared_ptr<ByteAddressBuffer>> m_UpdateIndexBuffers;
		std::queue<std::pair<uint64_t, uint32_t>> m_UpdateQueue;
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

	template <typename F, typename... Args>
	void foo(F&& f, Args&&... args)
	{
		[f, tuple=std::make_tuple(std::forward<Args>(args)...)]()
		{
			return std::apply(std::forward<F>(f), std::move(tuple));
		}();
	}
}
