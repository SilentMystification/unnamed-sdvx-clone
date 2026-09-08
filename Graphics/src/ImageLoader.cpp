#include "stdafx.h"
#include "ImageLoader.hpp"
#include "Image.hpp"

#ifdef __APPLE__
#include "libpng16/png.h"
#else
#include "png.h"
#endif

// HAVE_STDDEF_H redefinition
//#pragma warning(disable:4005)
#include "jpeglib.h"

namespace Graphics
{
	class ImageLoader_Impl
	{
		ImageLoader_Impl()
		{
		}
		~ImageLoader_Impl()
		{
		}
	public:

		// error handling
		struct jpegErrorMgr : public jpeg_error_mgr
		{
			jmp_buf jmpBuf;
		};
		static void jpegErrorExit(jpeg_common_struct* cinfo)
		{
			longjmp(((jpegErrorMgr*)cinfo->err)->jmpBuf, 1);
		}
		static void jpegErrorReset(jpeg_common_struct* cinfo)
		{
		}
		static void jpegEmitMessage(jpeg_common_struct* cinfo, int msgLvl)
		{
		}
		static void jpegOutputMessage(jpeg_common_struct* cinfo)
		{
		}
		static void jpegFormatMessage(jpeg_common_struct* cinfo, char * buffer)
		{
		}

		bool LoadJPEG(ImageRes* pImage, Buffer& in)
		{

			/* This struct contains the JPEG decompression parameters and pointers to
			* working space (which is allocated as needed by the JPEG library).
			*/
			jpeg_decompress_struct cinfo;
			jpegErrorMgr jerr = {};
			jerr.reset_error_mgr = &jpegErrorReset;
			jerr.error_exit = &jpegErrorExit;
			jerr.emit_message = &jpegEmitMessage;
			jerr.format_message = &jpegFormatMessage;
			jerr.output_message = &jpegOutputMessage;
			cinfo.err = &jerr;

			// Return point for long jump
			if(setjmp(jerr.jmpBuf) == 0)
			{
				jpeg_create_decompress(&cinfo);
				jpeg_mem_src(&cinfo, in.data(), (uint32)in.size());
				int res = jpeg_read_header(&cinfo, TRUE);

				jpeg_start_decompress(&cinfo);
				int row_stride = cinfo.output_width * cinfo.output_components;
				JSAMPARRAY sample = (*cinfo.mem->alloc_sarray)
					((j_common_ptr)&cinfo, JPOOL_IMAGE, row_stride, 1);

				Vector2i size = Vector2i(cinfo.output_width, cinfo.output_height);
				pImage->SetSize(size);
				Colori* pBits = pImage->GetBits();

				size_t pixelSize = cinfo.out_color_components;
				cinfo.out_color_space = JCS_RGB;

				while(cinfo.output_scanline < cinfo.output_height)
				{
					jpeg_read_scanlines(&cinfo, sample, 1);
					for(size_t i = 0; i < cinfo.output_width; i++)
					{
						memcpy(pBits + i, sample[0] + i * pixelSize, pixelSize);
						pBits[i].w = 0xFF;
					}

					pBits += size.x;
				}

				jpeg_finish_decompress(&cinfo);
				jpeg_destroy_decompress(&cinfo);
				return true;
			}
			
			// If we get here, the loading of the jpeg failed - jpegFormatMessage/etc are
			// no-op stubs above, so there's no real error text available here, only that
			// it failed.
			Log("LoadJPEG: decode failed (longjmp error path)", Logger::Severity::Warning);
			return false;
		}
		bool LoadPNG(ImageRes* pImage, Buffer& in)
		{
			png_image image;
			memset(&image, 0, (sizeof image));
			image.version = PNG_IMAGE_VERSION;

			if(png_image_begin_read_from_memory(&image, in.data(), in.size()) == 0)
			{
				Logf("LoadPNG: png_image_begin_read_from_memory failed: %s", Logger::Severity::Warning, image.message);
				return false;
			}

			image.format = PNG_FORMAT_RGBA;

			pImage->SetSize(Vector2i(image.width, image.height));
			Colori* pBuffer = pImage->GetBits();
			if(!pBuffer)
			{
				Log("LoadPNG: ImageRes::GetBits() returned null after SetSize", Logger::Severity::Warning);
				return false;
			}

			if((image.width * image.height * 4) != PNG_IMAGE_SIZE(image))
			{
				Logf("LoadPNG: size mismatch, %ux%u*4=%u vs PNG_IMAGE_SIZE=%u", Logger::Severity::Warning,
					image.width, image.height, image.width * image.height * 4, (uint32)PNG_IMAGE_SIZE(image));
				return false;
			}

			if(png_image_finish_read(&image, nullptr, pBuffer, 0, nullptr) == 0)
			{
				Logf("LoadPNG: png_image_finish_read failed: %s", Logger::Severity::Warning, image.message);
				return false;
			}

			png_image_free(&image);
			return true;
		}
		bool Load(ImageRes* pImage, const String& fullPath)
		{
			File f;
			if(!f.OpenRead(fullPath))
			{
				Logf("ImageLoader::Load: failed to open \"%s\"", Logger::Severity::Warning, fullPath);
				return false;
			}

			Buffer b(f.GetSize());
			f.Read(b.data(), b.size());
			if(b.size() < 4)
			{
				Logf("ImageLoader::Load: \"%s\" is too small to be a real image (%u bytes)", Logger::Severity::Warning, fullPath, (uint32)b.size());
				return false;
			}

			return Load(pImage, b);
		}

		bool Load(ImageRes* pImage, Buffer& b)
		{
			// Check for PNG based on first 4 bytes
			if (std::memcmp(b.data(), "\x89PNG", 4) == 0)
				return LoadPNG(pImage, b);
			else // jay-PEG ?
				return LoadJPEG(pImage, b);
		}

		static ImageLoader_Impl& Main()
		{
			static ImageLoader_Impl inst;
			return inst;
		}
	};


	bool ImageLoader::Load(ImageRes* pImage, const String& fullPath)
	{
		return ImageLoader_Impl::Main().Load(pImage, fullPath);
	}

	bool ImageLoader::Load(ImageRes* pImage, Buffer& b)
	{
		return ImageLoader_Impl::Main().Load(pImage, b);
	}
}