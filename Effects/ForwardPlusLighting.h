#pragma once
#include "pch.h"
#include "GpuBuffer.h"
#include "ColorBuffer.h"
#include "ShadowBuffer.h"
#include "RootSignature.h"
#include "PipelineState.h"

namespace Math
{
	class Camera;
}

namespace MyDirectX
{
	class ColorBuffer;
	class GraphicsContext;	

	class ForwardPlusLighting
	{
	public:
		static constexpr unsigned MaxLights = 128;
		static constexpr unsigned MinLightGridDim = 8;

		ForwardPlusLighting();
		~ForwardPlusLighting();

		ForwardPlusLighting(const ForwardPlusLighting&) = delete;
		ForwardPlusLighting(ForwardPlusLighting&&) = delete;

		void Init(ID3D12Device *pDevice);
		void Shutdown();
		
		void CreateRandomLights(ID3D12Device* pDevice, const Math::Vector3 minBound, const Math::Vector3 maxBound);
		void FillLightGrid(GraphicsContext& gfxContext, const Math::Camera& camera, uint64_t frameIndex);

		// must keep in sync with HLSL
		struct LightData
		{
			DirectX::XMFLOAT3 position;
			float radiusSq;
			DirectX::XMFLOAT3 color;
			uint32_t type;

			DirectX::XMFLOAT3 coneDir;
			DirectX::XMFLOAT2 coneAngles;
			// Math::Matrix4 shadowTextureMatrix;		// WARNING: Can't use 'Math::Matrix4' - SIMD instructions need 16Byte alignment !!!
			DirectX::XMFLOAT4X4 shadowTextureMatrix;
		};
		std::vector<LightData> m_LightData;

		uint32_t m_LightGridDim = 16;

		// light data
		StructuredBuffer m_LightBuffer;
		ByteAddressBuffer m_LightGrid;

		ByteAddressBuffer m_LightGridBitMask;
		uint32_t m_FirstConeLight;
		uint32_t m_FirstConeShadowedLight;

		// shadow
		ColorBuffer m_LightShadowArray;
		ShadowBuffer m_LightShadowTempBuffer;
		std::vector<Math::Matrix4> m_LightShadowMatrix;

		std::vector<Math::Matrix4> m_PointLightShadowMatrix;

		// Use of forward declaration of 'Math::Camera':
		// ERROR::error C2338: static_assert failed: 'can't delete an incomplete type'
		/**
		* Ref: https://developercommunity.visualstudio.com/t/unique-ptr-cant-delete-an-incomplete-type/1371585
		*	The problem doesn't arise from destruction of the object, but construction. The C++ Standard mandates that 
		* object constructors "potentially invoke" destructors of subobjects, which means they are considered to be used 
		* and must therefore be defined and well-formed even if never actually called. This is sensible considering that 
		* when construction of some objects throws an exception, subobjects already constructed must be destroyed before
		* allowing the exception to propagate out of a complete object's constructor.
		*	The workaround is straightforward. You must handle any constructors - notably including the default constructor
		* which is currently defined implicitly.
		*/ 
		std::unique_ptr<Math::Camera[]> m_PointLightShadowCamera;
		
		// ERROR::error C2036: 'Math::Camera *': unknown size
		// Ref: https://github.com/microsoft/STL/issues/2720
		// Now that C++20 has made vector's destructor constexpr, it must be instantiated by this potential use
		// std::vector<Math::Camera> m_PointLightShadowCamera;

		uint32_t m_ShadowDim = 512;

		// root signature
		RootSignature m_FillLightRS;
		// PSOs
		ComputePSO m_FillLightGridCS_8;
		ComputePSO m_FillLightGridCS_16;
		ComputePSO m_FillLightGridCS_24;
		ComputePSO m_FillLightGridCS_32;
		
	};

}

/**
	https://www.3dgep.com/forward-plus/#Forward
	## ForwardPlus Lighting
	Forward+ improves upon regular forward rendering by first determining which lights are overlapping which
area in screen space. During the shading phase, only the lights that are potentially overlapping the current
fragment need to be considered.
	
	the Forward+ technique consists primarily of these 3 passes:
	1. light culling; 2. opaque pass; 3. transparent pass
	#1.in the light culling pass, each light in the scene is sorted into screen space lists
	#2.in the opaque pass, the light list generated from the light culling pass is used to compute the lighting
for opaque geometry. In this pass, not all lights need to be considered for lighting, only the lights that were
previously sorted into the current fragments screen space tile need to be considered when computing the lighting.
	#3.the transparent pass is similar to the opaque pass except the light list used for computing lighting is
slightly different.

	>> Grid Frustums
	before light culling can occur, we need to compute the culling frustums that will be used to cull the lights
into the screen space tiles. Since the culling frustums are expressed in view space, they only need to be recomputed
if the dimension of the grid changes (for example, if the screen is resized) or the size of a tile changes.
	the screen is divided into a number of square tiles("light grids"). The tile size should not be chosen
arbitrarily but it should be chosen so that each tiel can be computed by a single thread group. The number of
threads in a thread group should be a multiple of 64 (to take advantage of dual warp schedulers available on 
modern GPU) and annot exceed 1024 threads per thread group.
	8x8, 16x16, 32x32

	if we were to view the tiles at an oblique angle, we can visualize the culling frustum that we need to compute.
	a view frustum is composed of 6 planes, but to perform the lighting culling we want to precompute the 4 
side planes for the frustum.
	to compute the left, right, top and bottom frustum planes we will use the following algorithm:
	1.compute the 4 corner points of the current tile in screen space
	2.transform the screen space corner points to the far clipping plane in view space
	3.build the frustum planes from the eye position and 2 other corner points
	4.store the computed frustum in a RWStructuredBuffer

	>> Light Culling
	the computation of the grid frustums only needs to be done once at the beginning of the application or
if the screen dimensions or the size of the tiles change but the light culling phase must occur every frame
that the camera moves or the position of a light moves or an object in the scene changes that affects the 
contents of the depth buffer. Any one of these events could occur so it is generally safe to perform light culling
each and every frame.
	the basic algorithm for performing light culling is as follows:
	1.compute the min and max depth values in view space for the tile
	2.cull the lights and record the lights into a light index list
	3.copy the light index list into global memory
	the first step of the algorithm is to compute the minimum and maximum depth values per tile of the light grid.
The minimum and maximum depth values will be used to compute the near and far planes for the culling frustum
	
	for transparent geometry, we can only clip light volumes that are behind the maximum depth planes, but we 
must consider all lights that are in front of all opaque geometry. The reason for this is that when performing
the depth pre-pass step to generate the depth texture which is used to determine the minimum and maximum depths
per tile, we cannot render transparent geometry into the depth buffer. If we did, then we would not correctly
light opaque geometry that is behind transparent geometry. The light solution to this problem is described in
an article titled "Tiled Forward Shading". In the light culling compute shader, 2 light lists will be generated
The first light list contains only the lights that are affecting opaque geometry. The second light list contains
only the lights that could affect transparent geometry. When performing final shading on opaque geometry then
i will send the first list and when rendering transparent geometry, i will send the second list to the fragment shader.

	>> Light List Data Structure
	the data structure that is used to store the per-tile light lists is described in the paper titled "Tiled Shading"
from Ola Olsson and Ulf Assarsson. Ola and Ulf describe a data structure in 2 parts. The first part is the 
*<light grid>* which is a 2D grid that stores an offset and a count of values stored in a *<light index list>*.
This technique is similar to that of an index buffer which refers to the indices of vertices in an vertex buffer.
	Light List: L0 L1 L2 L3 ...
	Light Index List: 0 1 2 4 1 2 ...
	Light Grid: offset + count

	the size of the light grid is based on the number of screen tiles that are used for light culling. The size
of the light index is based on the expected average number of overlapping lights per tile. For example, for a screen
resolution of 1280x720 and a tile size of 16x16 results in a 80x45 (3,600) light grid.	Assuming an average of 
200 lights per tile, this would require a light index list of 720,000 indices. Each light index cost 4 bytes 
(for a 32-bit unsigned integer) so the light list would consume 2.88MB of GPU memory. Since we need a separate
list for transparent and opaque geometry, this would consume a total o 5.76MB.
	to generate the light grid and the light index list, a group-shared light index list is first generated
in the compute shader. A global light index list counter is used to keep track of the current index into the 
global light index list. The global light index counter is atomically incremented so that no 2 thread groups
can use the same range in the global light index list. Once the thread group has "reserved" space in the global
light index list, the group-shared light index list is copied to the global light index list.
	"1.loop through the global light list and cull the lights against the current tile's culling frustum. If the light
is inside the frustum, the light index is added to the local light index list."
	"2.the current index in the global light index list is incremented by the number of lights that are contained
in the local light index list. The original value of the global light index list counter before being incremented
is stored in the local counter variable"
	"3.the light grid G is updated with the current tile's offset and count into the global light index list".
	"4.finally, the local light index list is copied into the global light index list."

	>> Frustum Culling
	to perform frustum culling on the light volumes, 2 frustum culling methods will be presented:
	1.Frustum-Sphere culling for point lights
	2.Frustum-Cone culling for spot lights

	> Frustum-Sphere Culling
	a sphere is considered to be "inside" a plane if it is fully contained in the negative half-space of the plane.
If a sphere is completely "inside" any of the frustum planes then it is outside of the frustum.

	> Frustum-Cone Culling
	a cone can be defined by its tip T, a normalized direction vector d, the height of the cone h and 
the radius of the base r.
	struct Cone
	{
		float3 T;	// cone tip
		float h;	// height of the cone
		float3 d;	// direction of the cone
		float r;	// bottom radius of the cone
	}
	to test if a cone is completely contained in the negative half-space of a plane, only 2 points need to be tested:
	1.the tip of the cone;	2.the point that is one the base of the cone that is farthest away from the plane in 
direction of f(n)
	-mf v = x*Dir + y*n , Dot(v, Dir) = 0 ->
	v = normalize(-Dot(Dir, n)*Dir + n)	<Dir != a*n>
*/
