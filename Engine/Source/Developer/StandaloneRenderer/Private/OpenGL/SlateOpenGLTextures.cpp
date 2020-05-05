// Copyright Epic Games, Inc. All Rights Reserved.

#include "OpenGL/SlateOpenGLTextures.h"
#include "OpenGL/SlateOpenGLRenderer.h"
#if PLATFORM_MAC
#include "Mac/OpenGL/SlateOpenGLMac.h"
#endif
#define USE_DEPRECATED_OPENGL_FUNCTIONALITY			(!PLATFORM_USES_GLES && !PLATFORM_LINUX)

GLuint FSlateOpenGLTexture::NullTexture = 0;

void FSlateOpenGLTexture::Init( GLenum TexFormat, const TArray<uint8>& TextureData )
{
	// Create a new OpenGL texture
	glGenTextures(1, &ShaderResource);
	CHECK_GL_ERRORS;

	// Ensure texturing is enabled before setting texture properties
#if USE_DEPRECATED_OPENGL_FUNCTIONALITY
	glEnable(GL_TEXTURE_2D);
#endif // USE_DEPRECATED_OPENGL_FUNCTIONALITY
	glBindTexture(GL_TEXTURE_2D, ShaderResource);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
#if USE_DEPRECATED_OPENGL_FUNCTIONALITY
	glTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE );
#endif // USE_DEPRECATED_OPENGL_FUNCTIONALITY

	// the raw data is in bgra or bgr
	const GLint Format = GL_BGRA;

	// Upload the texture data
	glTexImage2D( GL_TEXTURE_2D, 0, TexFormat, SizeX, SizeY, 0, Format, GL_UNSIGNED_INT_8_8_8_8_REV, TextureData.GetData() );
	bHasPendingResize = false;
	CHECK_GL_ERRORS;
}

void FSlateOpenGLTexture::Init( GLuint TextureID )
{
	ShaderResource = TextureID;
	bHasPendingResize = false;
}

void FSlateOpenGLTexture::ResizeTexture(uint32 Width, uint32 Height)
{
	SizeX = Width;
	SizeY = Height;
	bHasPendingResize = true;
}

void FSlateOpenGLTexture::UpdateTexture(const TArray<uint8>& Bytes)
{
	UpdateTextureRaw(Bytes.GetData(), FIntRect());
}

void FSlateOpenGLTexture::UpdateTextureThreadSafeRaw(uint32 Width, uint32 Height, const void* Buffer, const FIntRect& Dirty)
{
	if (SizeX != Width || SizeY != Height)
	{
		ResizeTexture(Width, Height);
	}
	UpdateTextureRaw(Buffer, Dirty);
}

void FSlateOpenGLTexture::UpdateTextureThreadSafeWithTextureData(FSlateTextureData* TextureData)
{
	UpdateTextureThreadSafeRaw(TextureData->GetWidth(), TextureData->GetHeight(), TextureData->GetRawBytesPtr());
	delete TextureData;
}


void FSlateOpenGLTexture::UpdateTextureRaw(const void* Buffer, const FIntRect& Dirty)
{
#if PLATFORM_MAC
	LockGLContext([NSOpenGLContext currentContext]);
#endif
	// Ensure texturing is enabled before setting texture properties
#if USE_DEPRECATED_OPENGL_FUNCTIONALITY
	glEnable(GL_TEXTURE_2D);
#endif // USE_DEPRECATED_OPENGL_FUNCTIONALITY
	glBindTexture(GL_TEXTURE_2D, ShaderResource);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
#if USE_DEPRECATED_OPENGL_FUNCTIONALITY
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
#endif // USE_DEPRECATED_OPENGL_FUNCTIONALITY
	
	// Upload the texture data
#if !PLATFORM_USES_GLES

	if (bHasPendingResize || Dirty.Area() == 0)
	{
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, SizeX, SizeY, 0, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8_REV, Buffer);
		bHasPendingResize = false;
	}
	else
	{
		glPixelStorei(GL_UNPACK_ROW_LENGTH, SizeX);
		glTexSubImage2D(GL_TEXTURE_2D, 0, Dirty.Min.X, Dirty.Min.Y, Dirty.Width(), Dirty.Height(), GL_RGBA, GL_UNSIGNED_INT_8_8_8_8_REV, (uint8*)Buffer + Dirty.Min.Y * SizeX * 4 + Dirty.Min.X * 4);
		glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	}
#else
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, SizeX, SizeY, 0, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8_REV, Buffer);
#endif
	CHECK_GL_ERRORS;
#if PLATFORM_MAC
	UnlockGLContext([NSOpenGLContext currentContext]);
#endif
}


FSlateFontTextureOpenGL::FSlateFontTextureOpenGL(uint32 Width, uint32 Height, const bool InIsGrayscale)
	: FSlateFontAtlas(Width, Height, InIsGrayscale) 
	, FontTexture(nullptr)
{
}

FSlateFontTextureOpenGL::~FSlateFontTextureOpenGL()
{
	delete FontTexture;
}

void FSlateFontTextureOpenGL::CreateFontTexture()
{
	// Generate an ID for this texture
	GLuint TextureID;
	glGenTextures(1, &TextureID);

	// Bind the texture so we can specify filtering and the data to use
	glBindTexture(GL_TEXTURE_2D, TextureID);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	GLint InternalFormat = GetGLTextureInternalFormat();
	GLint Format = GetGLTextureFormat();
	GLint Type = GetGLTextureType();

	// Upload the data to the texture
	glTexImage2D( GL_TEXTURE_2D, 0, InternalFormat, AtlasWidth, AtlasHeight, 0, Format, Type, NULL );

	// Create a new slate texture for use in rendering
	FontTexture = new FSlateOpenGLTexture( AtlasWidth, AtlasHeight );
	FontTexture->Init( TextureID );
}

void FSlateFontTextureOpenGL::ConditionalUpdateTexture()
{
	// The texture may not be valid when calling this as OpenGL must wait until after the first viewport has been created to create a texture
	if( bNeedsUpdate && FontTexture )
	{
		check(AtlasData.Num()>0);

		// Completely the texture data each time characters are added
		glBindTexture(GL_TEXTURE_2D, FontTexture->GetTypedResource() );

#if PLATFORM_MAC // Make this texture use a DMA'd client storage backing store on OS X, where these extensions always exist
				 // This avoids a problem on Intel & Nvidia cards that makes characters disappear as well as making the texture updates
				 // as fast as they possibly can be.
		glTextureRangeAPPLE(GL_TEXTURE_2D, AtlasData.Num(), AtlasData.GetData());
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_STORAGE_HINT_APPLE, GL_STORAGE_CACHED_APPLE);
		glPixelStorei(GL_UNPACK_CLIENT_STORAGE_APPLE, GL_TRUE);
#endif
		GLint InternalFormat = GetGLTextureInternalFormat();
		GLint Format = GetGLTextureFormat();
		GLint Type = GetGLTextureType();
		glTexImage2D( GL_TEXTURE_2D, 0, InternalFormat, AtlasWidth, AtlasHeight, 0, Format, Type, AtlasData.GetData() );
#if PLATFORM_MAC
		glPixelStorei(GL_UNPACK_CLIENT_STORAGE_APPLE, GL_FALSE);
#endif
		
		bNeedsUpdate = false;
	}
}

GLint FSlateFontTextureOpenGL::GetGLTextureInternalFormat() const
{
	if (IsGrayscale())
	{
#if USE_DEPRECATED_OPENGL_FUNCTIONALITY
		return GL_ALPHA;
#else
		return GL_RED;
#endif // USE_DEPRECATED_OPENGL_FUNCTIONALITY
	}
#if !PLATFORM_USES_GLES
	return GL_SRGB8_ALPHA8;
#else
	return GL_SRGB8_ALPHA8_EXT;
#endif
}

GLint FSlateFontTextureOpenGL::GetGLTextureFormat() const
{
	if (IsGrayscale())
	{
#if USE_DEPRECATED_OPENGL_FUNCTIONALITY
		return GL_ALPHA;
#else
		return GL_RED;
#endif // USE_DEPRECATED_OPENGL_FUNCTIONALITY
	}
	return GL_RGBA;
}

GLint FSlateFontTextureOpenGL::GetGLTextureType() const
{
	if (IsGrayscale())
	{
		return GL_UNSIGNED_BYTE;
	}
	return GL_UNSIGNED_INT_8_8_8_8_REV;
}
