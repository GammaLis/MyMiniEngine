#include "Material.h"

using namespace MyDirectX;

bool Material::operator==(const Material& other) const
{
	if (m_MatData != other.m_MatData)
		return false;

	if (m_TexBaseColorPath	!= other.m_TexBaseColorPath)	return false;
	if (m_TexMetalRoughPath != other.m_TexMetalRoughPath)	return false;
	if (m_TexNormalPath		!= other.m_TexNormalPath)		return false;
	if (m_TexEmissivePath	!= other.m_TexEmissivePath)		return false;
	if (m_TexOcclusionPath	!= other.m_TexOcclusionPath)	return false;

	return true;
}

bool Material::operator!=(const Material& other) const
{
	return !(*this == other);
}
