#pragma once
#include "pch.h"
#include "Color.h"
#include "Math/Vector.h"
#include "PipelineState.h"
#include "RootSignature.h"
#include <string>

namespace MyDirectX
{
	// 绘制文本
	class TextRenderer
	{
		friend class TextContext;

	public:
		class Font;

		// 因为此时Font尚未定义，需要定义默认构造函数（即使为空） -20-1-26
		// 否则编译器生成默认无参构造函数，构造map<x, unique_ptr<...>> 出错
		TextRenderer();
		~TextRenderer();

		/**
			initialize the text renderer's resources and designate the dimensions of the drawable view space.
		These dimensions do not have to match the actual pixel width and height of the viewport. Instead they
		create a coordinate space for placing text within the viewport. For instance, if you specify a 
		ViewWidth of 2.0f, then CursorX = 1.0f marks the middle of the viewport.
		*/
		void Init(ID3D12Device* pDevice);
		void Shutdown();

		const TextRenderer::Font* GetOrLoadFont(const std::wstring& fileName);

		// helper function -mf (to be deleted)
		D3D12_CPU_DESCRIPTOR_HANDLE GetDefaultFontTexture();

	private:
		std::map<std::wstring, std::unique_ptr<TextRenderer::Font>> m_LoadedFonts;

		RootSignature m_RootSignature;
		GraphicsPSO m_TextPSO[2];		// 0: R8G8B8A8_UNORM 1: R11G11B10_FLOAT - HDR?
		GraphicsPSO m_ShadowPSO[2];		// 0: R8G8B8A8_UNORM 1: R11G11B10_FLOAT
	};

	// class Color;
	class GraphicsContext;

	class TextContext
	{
	public:
		TextContext(GraphicsContext& gfxContext, float canvasWidth = 1920.0f, float canvasHeight = 1080.0f);

		GraphicsContext& GetCommandContext() const { return m_GfxContext; }

		// put settings back to the defaults
		void ResetSettings();

		/**
			Control various text properties
		*/
		// choose a font from the Fonts folder. Previously loaded fonts are cached in memory
		void SetFont(const std::wstring& fontName, float textSize = 0.0f);

		// resize the view space. This determines the coordinate space of the cursor position and font size.                          
		// You can always set the view size to the same dimensions regardless of actual display resolution. It is
		// assumed, however, that the aspect ratio of this virtual coordinates system matches 
		// the actual aspect ratio.
		void SetViewSize(float viewWidth, float viewHeight);

		// set the size of the text relative to the ViewHeight. The aspect of the text is preserved from
		// the TTF as long as the aspect ratio of the view space is the same as the actual viewport
		void SetTextSize(float pixelHeight);

		// move the cursor position - the upper left anchor for the text
		void ResetCursor(float x, float y);
		void SetLeftMargin(float x) { m_LeftMargin = x; }
		void SetCursorX(float x) { m_TextPosX = x; }
		void SetCursorY(float y) { m_TextPosY = y; }
		void NewLine() { m_TextPosX = m_LeftMargin; m_TextPosY += m_LineHeight; }
		float GetLeftMargin() { return m_LeftMargin; }
		float GetCursorX() { return m_TextPosX; }
		float GetCursorY() { return m_TextPosY; }

		// turn on or off drop shadow
		void EnableDropShadow(bool bEnable);

		// adjust shadow parameters
		void SetShadowOffset(float xPercent, float yPercent);
		void SetShadowParams(float opacity, float width);

		// set the color and transparency of text
		void SetColor(Color color);

		// get the amount to advance the Y position to begin a new line
		float GetVerticalSpacing() { return m_LineHeight; }

		/**
			Rendering commands
		*/
		// begin and end drawing commands
		void Begin(bool bEnableHDR = false);
		void End();

		// draw a string
		void DrawString(const std::wstring& str);
		void DrawString(const std::string& str);

		// a more powerful function which formats text like printf(). 
		// very slow by comparison, so use it only if you're going to format text anyway
		void DrawFormattedString(const wchar_t* format, ... );
		void DrawFormattedString(const char* format, ... );		

	private:
		// __declspec(align(16)) struct VertexShaderParams
		// or C++11 alignas - 设置内存对齐方式，最小8
		struct alignas(16) VertexShaderParams
		{
			Math::Vector4 ViewportTransform;
			float NormalizeX, NormalizeY, TextSize;
			float Scale, DstBorder;
			uint32_t SrcBorder;
		};

		// __declspec(align(16)) struct PixelShaderParams
		struct alignas(16) PixelShaderParams
		{
			Color TextColor;
			float ShadowOffsetX, ShadowOffsetY;
			float ShadowHardness;		// more than 1 will cause aliasing
			float ShadowOpacity;		// should make less opaque when making softer
			float HeightRange;		
		};

		void SetRenderState();

		// 16 byte structure to represent an entire glyph in the text vertex buffer
		// __declspec(align(16)) struct TextVert
		struct alignas(16) TextVert
		{
			float X, Y;				// upper-left glyph position in screen space
			uint16_t U, V, W, H;	// upper-left glyph UV and the width in texture space
		};

		/**
			A volatile specifier is a hint to a compiler that an object may change its value 
		in ways not specified by the language so that aggressive optimizations must be avoided.
		*/ 
		// 填充顶点数据
		UINT FillVertexBuffer(TextVert volatile* verts, const char* str, size_t stride, size_t slen);

		GraphicsContext& m_GfxContext;
		const TextRenderer::Font* m_CurFont;
		VertexShaderParams m_VSParams;
		PixelShaderParams m_PSParams;
		
		bool m_VSConstantBufferIsStale;		// tracks when the CB needs updating
		bool m_PSConstantBufferIsStale;		// tracks when the CB needs updating
		bool m_TextureIsStale;
		bool m_EnableShadow;

		float m_LeftMargin;
		float m_TextPosX;
		float m_TextPosY;
		float m_LineHeight;
		float m_ViewWidth;		// width of the drawable area
		float m_ViewHeight;		// height of the drawable area
		float m_ShadowOffsetX;	// percentage of the font's TextSize should the shadow be offset
		float m_ShadowOffsetY;	// percentage of the font's TextSize should the shadow be offset
		BOOL m_HDR;
	};
}
