#pragma once
#include "pch.h"

namespace MyDirectX
{
	enum class InputType
	{
		PerVertexData,	// buffer elements will represent per-vertex data
		PerInstanceData	// buffer elements will represent per-instance data
	};

	struct VertexAttrib
	{
		std::string semanticName;
		uint32_t semanticIndex = 0;
		DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
		uint32_t inputSlot = 0;
		uint32_t alignedByteOffset = 0;
	};

	class VertexBufferLayout
	{
	public:
		using SharedPtr = std::shared_ptr<VertexBufferLayout>;
		using SharedConstPtr = std::shared_ptr<const VertexBufferLayout>;

		// create a new vertex buffer layout object
		static SharedPtr Create()
		{
			return SharedPtr(new VertexBufferLayout());
		}

		VertexBufferLayout() = default;

		VertexBufferLayout& AddElement(const std::string& name, DXGI_FORMAT format, uint32_t offset = 0, uint32_t semanticIndex = 0, uint32_t inputSlot = 0)
		{
			VertexAttrib attrib;
			attrib.semanticName = name;
			attrib.semanticIndex = semanticIndex;
			attrib.format = format;
			attrib.alignedByteOffset = (offset == 0 ? m_VertexStride : offset);
			attrib.inputSlot = inputSlot;
			m_Elements.emplace_back(attrib);
			m_VertexStride += BytesPerFormat(format);	

			return *this;
		}

		const VertexAttrib& GetBufferLayout(size_t index) const
		{
			return m_Elements[index];
		}

		// return the element offset pointed to by index
		uint32_t GetElementOffset(uint32_t index) const { return m_Elements[index].alignedByteOffset; }

		// return the element format pointed to by index
		DXGI_FORMAT GetElementFormat(uint32_t index) const { return m_Elements[index].format; }

		// return the semantic name of the element
		const std::string& GetElementName(uint32_t index) const { return m_Elements[index].semanticName; }

		// return the number of elements in the object
		uint32_t GetElementCount() const { return (uint32_t)m_Elements.size(); }

		// return the total stride of all elements in bytes
		uint32_t GetStride() const { return m_VertexStride; }

		// return the input classification
		InputType GetInputClass() const { return m_InputType; }

		// return the per-instance data step rate
		uint32_t GetInstanceStepRate() const { return m_InstanceDataStepRate; }

		// set the input class and the data step rate
		// inputType - specifies is this layout object holds per-vertex or per-instance data
		// instanceStepRate - for per-instance data, specifies how many instance to draw using the same per-instance data.
		// if this is 0, it behaves as if the class if PerVertexData
		void SetInputClass(InputType inputType, uint32_t stepRate)
		{
			m_InputType = inputType;
			m_InstanceDataStepRate = stepRate;
		}

	private:
		std::vector<VertexAttrib> m_Elements;
		InputType m_InputType = InputType::PerVertexData;
		uint32_t m_InstanceDataStepRate = 0;
		uint32_t m_VertexStride = 0;
	};
}
