#pragma once
#include "pch.h"
#include "Math/GLMath.h"

namespace MFalcor
{
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
		uint32_t vertexByteOffset = 0;
		uint32_t vertexByteSize = 0;
		uint32_t vertexStrideSize = 0;
		uint32_t vertexCount = 0;

		uint32_t indexByteOffset = 0;
		uint32_t indexByteSize = 0;
		uint32_t indexStrideSize = 16;	// uint16_t
		uint32_t indexCount = 0;

		uint32_t materialID = 0;
	};

	class Scene
	{
	public:
		using SharedPtr = std::shared_ptr<Scene>;
		using ConstSharedPtrRef = const SharedPtr&;

		static const uint32_t kMaxBonesPerVertex = 4;

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

		/// ** Camera **
		// access the scene's camera to change properties, or use elsewhere
		void GetCamera() const;

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

		/// ** Mesh **
		// get the number of meshes
		uint32_t GetMeshCount() const {}

		// get a mesh desc
		void GetMesh(uint32_t meshId) const;

		// get the number of mesh instances
		uint32_t GetMeshInstanceCount() const;

		/// ** Material **
		// get a material
		void GetMaterial(uint32_t materialId) const;

		// get a material by name
		void GetMaterialByName(const std::string& name) const;

		/// ** Scene **
		// get the scene bounds
		const BoundingBox& GetSceneBounds() const;

		// get a mesh's bounds
		const BoundingBox& GetMeshBounds(uint32_t meshId) const;

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

		/// ** Framework ** 
		UpdateFlags Update(float deltaTime);

		// get the changes that happended during the last update
		// the flags only change during an `Update` call, if something changed between calling `Update` and `GetUpdates()`,
		// the returned result will not reflect it 
		UpdateFlags GetUpdates() const;

		// render the scene using the rasterizer
		void Render();

		// render the scene using raytracing
		void Raytrace();

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



	};

}
