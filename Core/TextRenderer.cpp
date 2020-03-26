#include "TextRenderer.h"
#include "TextureManager.h"
#include "Graphics.h"
#include "GfxCommon.h"
#include "CommandContext.h"
#include "Effects.h"
#include "../FileUtility.h"
#include "../Fonts/consola24.h"

namespace MyDirectX
{
	// TextRenderer
	class TextRenderer::Font
	{
	public:
		Font()
		{
			m_NormalizeXCoord = 0.0f;
			m_NormalizeYCoord = 0.0f;
			m_FontLineSpacing = 0.0f;
			m_AntialiasRange = 0.0f;
			m_FontHeight = 0;
			m_BorderSize = 0;
			m_TextureWidth = 0;
			m_TextureHeight = 0;
		}

		~Font()
		{
			m_Dict.clear();
		}

		void LoadFromBinary(const wchar_t* fontName, const uint8_t* pBinary, const size_t binarySize)
		{
			(fontName);

			// we should at least use this to assert that we have a complete file
			(binarySize);

			// 头部信息
			struct FontHeader
			{
				char FileDescriptor[8];		// "SDFFONT\0"
				uint8_t majorVersion;		// '1'
				uint8_t minorVersion;		// '0'
				uint16_t borderSize;		// pixel empty space border width
				uint16_t textureWidth;		// width of texture buffer
				uint16_t textureHeight;		// height of texture buffer
				uint16_t fontHeight;		// font height in 12.4
				uint16_t advanceY;			// line height in 12.4
				uint16_t numGlyphs;			// Glyph count in texture
				uint16_t searchDist;		// range of search space 12.4
			};
			FontHeader* header = (FontHeader*)pBinary;
			m_NormalizeXCoord = 1.0f / (header->textureWidth * 16);
			m_NormalizeYCoord = 1.0f / (header->textureHeight * 16);
			m_FontHeight = header->fontHeight;
			m_FontLineSpacing = (float)header->advanceY / (float)header->fontHeight;
			m_BorderSize = header->borderSize * 16;
			m_AntialiasRange = (float)header->searchDist / header->fontHeight;
			uint16_t textureWidth = header->textureWidth;
			uint16_t textureHeight = header->textureHeight;
			uint16_t numGlyphs = header->numGlyphs;

			const wchar_t* wcharList = (wchar_t*)(pBinary + sizeof(FontHeader));
			const Glyph* glyphData = (Glyph*)(wcharList + numGlyphs);
			const void* texelData = glyphData + numGlyphs;

			for (uint16_t i = 0; i < numGlyphs; ++i)
			{
				m_Dict[wcharList[i]] = glyphData[i];
			}
			
			// SDF font texture
			m_Texture.Create(Graphics::s_Device, textureWidth, textureHeight, DXGI_FORMAT_R8_SNORM, texelData);

			DEBUGPRINT("Loaded SDF font: %ls (ver. %d.%d)", fontName, header->majorVersion, header->minorVersion);

		}

		bool Load(const std::wstring& fileName)
		{
			Utility::ByteArray ba = Utility::ReadFileSync(fileName);

			if (ba->size() == 0)
			{
				ERROR("Cannot open file %ls", fileName.c_str());
				return false;
			}

			LoadFromBinary(fileName.c_str(), ba->data(), ba->size());

			return true;
		}

		// each character has an XY start offset, a width, and they all share the same height
		// 字形
		struct Glyph
		{
			uint16_t x, y, w;
			uint16_t bearing;
			uint16_t advance;
		};

		const Glyph* GetGlyph(wchar_t ch) const
		{
			auto iter = m_Dict.find(ch);
			return iter == m_Dict.end() ? nullptr : &iter->second;
		}

		// get the texel height of the font in 12.4 fixed point
		uint16_t GetHeight() const { return m_FontHeight; }

		// get the size of the border in 12.4 fixed point
		uint16_t GetBorderSize() const { return m_BorderSize; }

		// get the line advance height given a certain font size
		float GetVerticalSpacing(float size) const { return size * m_FontLineSpacing; }

		// get the texture object
		const Texture& GetTexture() const { return m_Texture; }

		float GetXNormalizationFactor() const { return m_NormalizeXCoord; }
		float GetYNormalizationFactor() const { return m_NormalizeYCoord; }

		// get the range in terms of height values centered on the midline that represents a pixel in
		// screen space (according to the specified font size.)
		// the pixel alpha should range from 0 to 1 over the height range 0.5 +/- 0.5 * aaRange
		float GetAntialiasingRange(float size) const { return Math::Max(1.0f, size * m_AntialiasRange); }

	private:
		float m_NormalizeXCoord;
		float m_NormalizeYCoord;
		float m_FontLineSpacing;
		float m_AntialiasRange;
		uint16_t m_FontHeight;
		uint16_t m_BorderSize;
		uint16_t m_TextureWidth;
		uint16_t m_TextureHeight;
		Texture m_Texture;
		std::map<wchar_t, Glyph> m_Dict;
	};

	const TextRenderer::Font* TextRenderer::GetOrLoadFont(const std::wstring& fileName)
	{
		auto iter = m_LoadedFonts.find(fileName);
		if (iter != m_LoadedFonts.end())
			return iter->second.get();

		Font* newFont = new Font();
		if (fileName == L"Default")
		{
			newFont->LoadFromBinary(L"Default", g_pconsola24, sizeof(g_pconsola24));
		}
		else
		{
			newFont->Load(L"Fonts/" + fileName + L".fnt");
		}
		m_LoadedFonts[fileName].reset(newFont);
		return newFont;
	}

	D3D12_CPU_DESCRIPTOR_HANDLE TextRenderer::GetDefaultFontTexture()
	{
		auto iter = m_LoadedFonts.find(L"Default");
		if (iter != m_LoadedFonts.end())
		{
			return iter->second->GetTexture().GetSRV();
		}
		return D3D12_CPU_DESCRIPTOR_HANDLE();
	}

	TextRenderer::TextRenderer() {	}

	TextRenderer::~TextRenderer() {  }

	void TextRenderer::Init(ID3D12Device* pDevice)
	{
		// root signatures
		m_RootSignature.Reset(3, 1);
		m_RootSignature[0].InitAsConstantBuffer(0, 0, D3D12_SHADER_VISIBILITY_VERTEX);
		m_RootSignature[1].InitAsConstantBuffer(0, 0, D3D12_SHADER_VISIBILITY_PIXEL);
		m_RootSignature[2].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 1, D3D12_SHADER_VISIBILITY_PIXEL);
		m_RootSignature.InitStaticSampler(0, Graphics::s_CommonStates.SamplerLinearClampDesc, D3D12_SHADER_VISIBILITY_PIXEL);
		m_RootSignature.Finalize(pDevice, L"TextRenderer", D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		// input element
		// the glyph vertex description. One vertex will correspond to a single character
		// 顶点输入 - instance data，vertex data - 标准四边形
		D3D12_INPUT_ELEMENT_DESC vertElem[] =
		{
			{"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,	   0, 0, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
			{"TEXCOORD", 0, DXGI_FORMAT_R16G16B16A16_UINT, 0, 8, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
		};
		m_TextPSO[0].SetRootSignature(m_RootSignature);
		m_TextPSO[0].SetInputLayout(_countof(vertElem), vertElem);
		m_TextPSO[0].SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
		m_TextPSO[0].SetVertexShader(Graphics::s_ShaderManager.m_TextVS);
		m_TextPSO[0].SetPixelShader(Graphics::s_ShaderManager.m_TextAntialiasPS);
		m_TextPSO[0].SetRasterizerState(Graphics::s_CommonStates.RasterizerTwoSided);
		m_TextPSO[0].SetBlendState(Graphics::s_CommonStates.BlendPreMultiplied);
		m_TextPSO[0].SetDepthStencilState(Graphics::s_CommonStates.DepthStateDisabled);
		m_TextPSO[0].SetRenderTargetFormats(1, &Graphics::s_BufferManager.m_OverlayBuffer.GetFormat(), DXGI_FORMAT_UNKNOWN);
		m_TextPSO[0].Finalize(pDevice);

		m_TextPSO[1] = m_TextPSO[0];
		m_TextPSO[1].SetRenderTargetFormats(1, &Graphics::s_BufferManager.m_SceneColorBuffer.GetFormat(), DXGI_FORMAT_UNKNOWN);
		m_TextPSO[1].Finalize(pDevice);

		m_ShadowPSO[0] = m_TextPSO[0];
		m_ShadowPSO[0].SetPixelShader(Graphics::s_ShaderManager.m_TextShadowPS);
		m_ShadowPSO[0].Finalize(pDevice);

		m_ShadowPSO[1] = m_ShadowPSO[0];
		m_ShadowPSO[1].SetRenderTargetFormats(1, &Graphics::s_BufferManager.m_SceneColorBuffer.GetFormat(), DXGI_FORMAT_UNKNOWN);
		m_ShadowPSO[1].Finalize(pDevice);

	}

	void TextRenderer::Shutdown()
	{
		m_LoadedFonts.clear();
	}

	// TextContext
	TextContext::TextContext(GraphicsContext& gfxContext, float canvasWidth, float canvasHeight)
		: m_GfxContext(gfxContext)
	{
		m_HDR = FALSE;
		m_CurFont = nullptr;
		m_ViewWidth = canvasWidth;
		m_ViewHeight = canvasHeight;

		// transform from text view space to clip space
		// x' =  2/W * x + (-vpx * 2/W - 1), 
		// y' = -2/H * y + ( vpy * 2/H + 1)
		const float vpX = 0.0f;		// viewport top left x
		const float vpY = 0.0f;		// viewport top left y
		const float twoDivW = 2.f / canvasWidth;
		const float twoDivH = 2.f / canvasHeight;
		m_VSParams.ViewportTransform = Math::Vector4(twoDivW, -twoDivH, -vpX * twoDivW - 1.0f, vpY * twoDivH + 1.0f);

		// the font texture dimensions are still unknown
		m_VSParams.NormalizeX = 1.0f;
		m_VSParams.NormalizeY = 1.0f;

		ResetSettings();
	}

	void TextContext::ResetSettings()
	{
		m_EnableShadow = true;
		ResetCursor(0.0f, 0.0f);
		m_ShadowOffsetX = 0.05f;
		m_ShadowOffsetY = 0.05f;
		m_PSParams.ShadowHardness = 0.5f;
		m_PSParams.ShadowOpacity = 1.0f;
		m_PSParams.TextColor = Color(1.0f, 1.0f, 1.0f, 1.0f);

		m_VSConstantBufferIsStale = true;
		m_PSConstantBufferIsStale = true;
		m_TextureIsStale = true;

		SetFont(L"Default", 24.0f);
	}

	void TextContext::SetFont(const std::wstring& fontName, float textSize)
	{
		// if that font is already set or doesn't exist, return
		const TextRenderer::Font* nextFont = Effects::s_TextRenderer.GetOrLoadFont(fontName);
		if (nextFont == m_CurFont || nextFont == nullptr)
		{
			if (textSize > 0.0f)
				SetTextSize(textSize);

			return;
		}

		m_CurFont = nextFont;

		// check to see if a new size was specified
		if (textSize > 0.0f)
			m_VSParams.TextSize = textSize;

		// update constants directly tied to the font or the font size
		m_LineHeight = nextFont->GetVerticalSpacing(m_VSParams.TextSize);
		m_VSParams.NormalizeX = m_CurFont->GetXNormalizationFactor();
		m_VSParams.NormalizeY = m_CurFont->GetYNormalizationFactor();
		m_VSParams.Scale = m_VSParams.TextSize / m_CurFont->GetHeight();
		m_VSParams.DstBorder = m_CurFont->GetBorderSize() * m_VSParams.Scale;
		m_VSParams.SrcBorder = m_CurFont->GetBorderSize();
		m_PSParams.ShadowOffsetX = m_CurFont->GetHeight() * m_ShadowOffsetX * m_VSParams.NormalizeX;
		m_PSParams.ShadowOffsetY = m_CurFont->GetHeight() * m_ShadowOffsetY * m_VSParams.NormalizeY;
		m_PSParams.HeightRange = m_CurFont->GetAntialiasingRange(m_VSParams.TextSize);
		m_VSConstantBufferIsStale = true;
		m_PSConstantBufferIsStale = true;
		m_TextureIsStale = true;

	}

	void TextContext::SetViewSize(float viewWidth, float viewHeight)
	{
		m_ViewWidth = viewWidth;
		m_ViewHeight = viewHeight;

		const float vpX = 0.0f;
		const float vpY = 0.0f;
		const float twoDivW = 2.0f / viewWidth;
		const float twoDivH = 2.0f / viewHeight;

		// essentially transform screen coordinates to clip space with W = 1.
		m_VSParams.ViewportTransform = Math::Vector4(twoDivW, -twoDivH, -vpX * twoDivW - 1.0f, vpY * twoDivH + 1.0f);
		m_VSConstantBufferIsStale = true;
	}

	void TextContext::SetTextSize(float pixelHeight)
	{
		if (m_VSParams.TextSize == pixelHeight)
			return;

		m_VSParams.TextSize = pixelHeight;
		m_VSConstantBufferIsStale = true;

		if (m_CurFont != nullptr)
		{
			m_PSParams.HeightRange = m_CurFont->GetAntialiasingRange(m_VSParams.TextSize);
			m_VSParams.Scale = m_VSParams.TextSize / m_CurFont->GetHeight();
			m_VSParams.DstBorder = m_CurFont->GetBorderSize() * m_VSParams.Scale;
			m_PSConstantBufferIsStale = true;
			m_LineHeight = m_CurFont->GetVerticalSpacing(pixelHeight);
		}
		else
			m_LineHeight = 0.0f;
	}

	void TextContext::ResetCursor(float x, float y)
	{
		m_LeftMargin = x;
		m_TextPosX = x;
		m_TextPosY = y;
	}

	void TextContext::EnableDropShadow(bool bEnable)
	{
		if (m_EnableShadow == bEnable)
			return;

		m_EnableShadow = bEnable;
		m_GfxContext.SetPipelineState(m_EnableShadow ?
			Effects::s_TextRenderer.m_ShadowPSO[m_HDR] : Effects::s_TextRenderer.m_ShadowPSO[m_HDR]);
	}

	void TextContext::SetShadowOffset(float xPercent, float yPercent)
	{
		m_ShadowOffsetX = xPercent;
		m_ShadowOffsetY = yPercent;
		m_PSParams.ShadowOffsetX = m_CurFont->GetHeight() * m_ShadowOffsetX * m_VSParams.NormalizeX;
		m_PSParams.ShadowOffsetY = m_CurFont->GetHeight() * m_ShadowOffsetY * m_VSParams.NormalizeY;
		m_PSConstantBufferIsStale = true;
	}

	void TextContext::SetShadowParams(float opacity, float width)
	{
		m_PSParams.ShadowHardness = 1.0f / width;
		m_PSParams.ShadowOpacity = opacity;
		m_PSConstantBufferIsStale = true;
	}

	void TextContext::SetColor(Color color)
	{
		m_PSParams.TextColor = color;
		m_PSConstantBufferIsStale = true;
	}

	void TextContext::Begin(bool bEnableHDR)
	{
		ResetSettings();

		m_HDR = (BOOL)bEnableHDR;

		m_GfxContext.SetRootSignature(Effects::s_TextRenderer.m_RootSignature);
		m_GfxContext.SetPipelineState(Effects::s_TextRenderer.m_ShadowPSO[m_HDR]);
		m_GfxContext.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	}

	void TextContext::End()
	{
		m_VSConstantBufferIsStale = true;
		m_PSConstantBufferIsStale = true;
		m_TextureIsStale = true;
	}

	void TextContext::DrawString(const std::wstring& str)
	{
		SetRenderState();

		void* stackMem = _malloca((str.size() + 1) * 16);
		TextVert* vbPtr = Math::AlignUp((TextVert*)stackMem, 16);
		UINT primCount = FillVertexBuffer(vbPtr, (char*)str.c_str(), 2, str.size());

		if (primCount > 0)
		{
			m_GfxContext.SetDynamicVB(0, primCount, sizeof(TextVert), vbPtr);
			m_GfxContext.DrawInstanced(4, primCount);
		}

		_freea(stackMem);
	}

	void TextContext::DrawString(const std::string& str)
	{
		SetRenderState();

		void* stackMem = _malloca((str.size() + 1) * 16);
		TextVert* vbPtr = Math::AlignUp((TextVert*)stackMem, 16);
		UINT primCount = FillVertexBuffer(vbPtr, (char*)str.c_str(), 1, str.size());

		if (primCount > 0)
		{
			m_GfxContext.SetDynamicVB(0, primCount, sizeof(TextVert), vbPtr);
			m_GfxContext.DrawInstanced(4, primCount);
		}

		_freea(stackMem);		
	}

	void TextContext::DrawFormattedString(const wchar_t* format, ...)
	{
		wchar_t buffer[256];
		va_list ap;
		va_start(ap, format);
		vswprintf(buffer, 256, format, ap);
		DrawString(std::wstring(buffer));
	}

	void TextContext::DrawFormattedString(const char* format, ...)
	{
		char buffer[256];
		va_list ap;
		va_start(ap, format);
		vsprintf_s(buffer, 256, format, ap);
		DrawString(std::string(buffer));
	}

	void TextContext::SetRenderState()
	{
		WARN_ONCE_IF(m_CurFont == nullptr, "Attempted to draw text without a font");

		if (m_VSConstantBufferIsStale)
		{
			m_GfxContext.SetDynamicConstantBufferView(0, sizeof(m_VSParams), &m_VSParams);
			m_VSConstantBufferIsStale = false;
		}

		if (m_PSConstantBufferIsStale)
		{
			m_GfxContext.SetDynamicConstantBufferView(1, sizeof(m_PSParams), &m_PSParams);
			m_PSConstantBufferIsStale = false;
		}

		if (m_TextureIsStale)
		{
			m_GfxContext.SetDynamicDescriptors(2, 0, 1, &m_CurFont->GetTexture().GetSRV());
			m_TextureIsStale = false;
		}
	}

	// these are made with templates to handle char and wchar_t simutianeously
	UINT TextContext::FillVertexBuffer(TextVert volatile* verts, const char* str, size_t stride, size_t slen)
	{
		UINT charsDrawn = 0;

		const float UVtoPixel = m_VSParams.Scale;

		float curX = m_TextPosX;
		float curY = m_TextPosY;

		const uint16_t texelHeight = m_CurFont->GetHeight();

		const char* iter = str;
		for (size_t i = 0; i < slen; ++i)
		{
			wchar_t wc = (stride == 2 ? *(wchar_t*)iter : *iter);
			iter += stride;

			// terminate on null character (this really shouldn't happen with string or wstring)
			if (wc == L'\0')
				break;

			// handle newlines by inserting a carriage return and line feed
			if (wc == L'\n')
			{
				curX = m_LeftMargin;
				curY += m_LineHeight;
				continue;
			}

			const TextRenderer::Font::Glyph* gi = m_CurFont->GetGlyph(wc);

			// ignore missing characters
			if (gi == nullptr)
				continue;

			verts->X = curX + (float)gi->bearing * UVtoPixel;
			verts->Y = curY;
			verts->U = gi->x;
			verts->V = gi->y;
			verts->W = gi->w;
			verts->H = texelHeight;
			++verts;

			// advance the cursor position
			curX += (float)gi->advance * UVtoPixel;
			++charsDrawn;
		}

		m_TextPosX = curX;
		m_TextPosY = curY;

		return charsDrawn;
	}

}

/**
	https://developer.apple.com/fonts/TrueType-Reference-Manual/RM01/Chap1.html
	Digitizing Letterform Designs
	>> Points
	at the lowest level, each glyph in a TrueType font is described by a sequence of points on a grid.
While 2 on-curve points are sufficient to describe a straight line, the addition of a third off-curve point
between 2 on-curve points makes it possible to describe a parabolic curve（抛物曲线）.In such cases, each
of the on-curve points represents an end point of the curve and the off-curve point is a control point.
Changing the location of any of the 3 points changes the shape of the curve defined.
	the definition of such a curve can be made formal as follows: given 3 points p0, p1 and p2, they define
a curve from point p0 to point p2 with p1 an off-curve point. The control point p1 is at the point of intersection
of the tangents to the curve at points p0 and p2.
	p(t) = (1-t)^2 * p0 + 2t * (1-t) * p1 + t^2 * p2	- 即 贝塞尔曲线

	by combining curves and straight lines, it is possible to build up complex glyphs. 

	>> The direction of contours:
	the points in a contour must be ordered consecutively begining with, in the case of the first contour, 
point 0. Subsequent contours will begin with the first unused number. It must be possible to trace around 
each contour by going from point to point along the contour in the order specified in the font file.
	the order in which points are specified is significant because it determines the direction of the contour.
The direction is always from lower point number toward higher point number.
	the direction of a glyph's contours is used to determine which portions of the shape defined by the contours
is filled(black) and which portions are unfilled (white).
*/