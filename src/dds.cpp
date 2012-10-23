////////////////////////////////////////////////////////////////
//
// RWGTEX - DDS exporting
// coded by Pavel [VorteX] Timofeyev and placed to public domain
//
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
//
// See the GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
////////////////////////////////

#include "main.h"
#include "dds.h"

// NVidia DXT compressor
#include <dxtlib/dxtlib.h>
#pragma comment(lib, "nvDXTlibMT.vc8.lib")

// NVIdia Texture Tools
#include <nvtt.h>
#pragma comment(lib, "nvtt.lib")

// ATI Compress
#include <ATI_Compress.h>
#include <ATI_Compress_Test_Helpers.cpp>
#pragma comment(lib, "ATI_Compress_MT.lib")

char opt_srcDir[MAX_FPATH];
char opt_srcFile[MAX_FPATH];
char opt_destPath[MAX_FPATH];

char *getCompressionFormatString(DWORD formatCC)
{
	if (formatCC == FORMAT_DXT1) return "DXT1";
	if (formatCC == FORMAT_DXT2) return "DXT2";
	if (formatCC == FORMAT_DXT3) return "DXT3";
	if (formatCC == FORMAT_DXT4) return "DXT4";
	if (formatCC == FORMAT_DXT5) return "DXT5";
	if (formatCC == FORMAT_BGRA) return "BGRA";
	if (formatCC == FORMAT_RXGB) return "RXGB";
	return "UNKNOWN";
}

double getCompressionRatio(DWORD formatCC)
{
	if (formatCC == FORMAT_BGRA)
		return 1.0f;
	if (formatCC == FORMAT_DXT1)
		return 1.0f/8.0f;
	return 1.0f/4.0f;
}

size_t CompressedSize(LoadedImage *image, DWORD formatCC, bool ddsHeader, bool baseTex, bool mipMaps)
{
	size_t size, block, blockBytes;
	int x, y;

	if (formatCC == FORMAT_BGRA)
	{
		block = 1;
		blockBytes = 4;
	}
	else
	{
		block = 4;
		blockBytes = (size_t)(64*getCompressionRatio(formatCC));
	}
	// header
	size = ddsHeader ? (sizeof(DWORD) + sizeof(DDSD2)) : 0;
	// base layer
	if (baseTex)
	{
		x = (int)ceil((float)image->width / block);
		y = (int)ceil((float)image->height / block);
		size += x*y*blockBytes;
	}
	// mipmaps
	if (mipMaps)
	{
		for (MipMap *mipmap = image->mipMaps; mipmap; mipmap = mipmap->nextmip)
		{
			x = (int)ceil((float)mipmap->width / block);
			y = (int)ceil((float)mipmap->height / block);
			size += x*y*blockBytes;
		}
	}
	return size;
}

bool IsDXT5SwizzledFormat(DWORD formatCC)
{
   if (formatCC == FOURCC_DXT5_xGBR || formatCC == FOURCC_DXT5_RxBG || formatCC == FOURCC_DXT5_RBxG ||
       formatCC == FOURCC_DXT5_xRBG || formatCC == FOURCC_DXT5_RGxB || formatCC == FOURCC_DXT5_xGxR ||
       formatCC == FOURCC_ATI2N_DXT5)
       return true;
   return false;
}

byte *DDSHeader(byte *in, LoadedImage *image, DWORD formatCC)
{
	DDSD2 dds;
	
	memcpy(in, &DDS_HEADER, sizeof(DWORD));
	in += sizeof(DWORD);

	// write header
	memset(&dds, 0, sizeof(dds));
	dds.dwSize = sizeof(DDSD2);
	dds.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT | DDSD_MIPMAPCOUNT;
	dds.dwWidth = image->width;
	dds.dwHeight = image->height;
	dds.dwMipMapCount = 1;
	for (MipMap *mipmap = image->mipMaps; mipmap; mipmap = mipmap->nextmip) dds.dwMipMapCount++;
	dds.ddpfPixelFormat.dwSize = sizeof(DDPIXELFORMAT);
	dds.ddsCaps.dwCaps = DDSCAPS_TEXTURE | DDSCAPS_COMPLEX | DDSCAPS_MIPMAP;
	if (formatCC == FORMAT_BGRA)
	{
		dds.lPitch = dds.dwWidth * 4;
		dds.ddpfPixelFormat.dwRBitMask = 0x00ff0000;
		dds.ddpfPixelFormat.dwGBitMask = 0x0000ff00;
		dds.ddpfPixelFormat.dwBBitMask = 0x000000ff;
		dds.ddpfPixelFormat.dwRGBBitCount = 32;
        dds.ddpfPixelFormat.dwFlags = DDPF_ALPHAPIXELS | DDPF_RGB;
		dds.ddpfPixelFormat.dwRGBAlphaBitMask = 0xff000000;
	}
	else
	{
		dds.ddpfPixelFormat.dwFourCC = formatCC;
		dds.ddpfPixelFormat.dwFlags = DDPF_FOURCC;
	}

	// fill alphapixels information, ensure that our texture have actual alpha channel
	// also texture may truncate it's alpha by forcing DXT1 compression no it
	if (image->hasAlpha)
		dds.ddpfPixelFormat.dwFlags |= DDPF_ALPHAPIXELS;

	// fill alphapremult
	if (formatCC == FORMAT_DXT2 || formatCC == FORMAT_DXT4)
		dds.ddpfPixelFormat.dwFlags |= DDPF_ALPHAPREMULT;
		
	// is we writing swizzled DXT5 format
	if (IsDXT5SwizzledFormat(formatCC))
	{
		dds.ddpfPixelFormat.dwPrivateFormatBitCount = dds.ddpfPixelFormat.dwFourCC;
		dds.ddpfPixelFormat.dwFourCC = FORMAT_DXT5;
	}
	
	memcpy(in, &dds, sizeof(DDSD2));
	in += sizeof(DDSD2);
	return in;
}

/*
==========================================================================================

  DDS compression - nvDXTlib path

==========================================================================================
*/

typedef struct
{
	byte        *stream;
	int          numwrites;
}nvWriteInfo;

NV_ERROR_CODE NvDXTLib_WriteDDS(const void *buffer, size_t count, const MIPMapData *mipMapData, void *userData)
{
	nvWriteInfo *wparm = (nvWriteInfo *)userData;
	if (wparm->numwrites > 1)
	{
		memcpy(wparm->stream, buffer, count);
		wparm->stream = (byte *)(wparm->stream) + count;
	}
	wparm->numwrites++;
	return NV_OK;
}

bool NvDXTlibCompress(byte *stream, FS_File *file, LoadedImage *image, DWORD formatCC)
{
	nvCompressionOptions options;
	nvWriteInfo writeOptions;
	float weights[3];

	// options
	options.SetDefaultOptions();
	options.DoNotGenerateMIPMaps();
	options.SetQuality(kQualityHighest, 100);
	options.user_data = &writeOptions;
	if (image->useChannelWeighting)
	{
		weights[0] = image->weightRed;
		weights[1] = image->weightGreen;
		weights[2] = image->weightBlue;
		options.SetCompressionWeighting(kUserDefinedWeighting, weights);
	}
	if (formatCC == FORMAT_DXT1)
	{
		if (image->hasAlpha)
		{
			options.bForceDXT1FourColors = false;
			options.SetTextureFormat(kTextureTypeTexture2D, kDXT1a);
		}
		else
		{
			options.bForceDXT1FourColors = true;
			options.SetTextureFormat(kTextureTypeTexture2D, kDXT1);
		}
	}
	else if (formatCC == FORMAT_DXT2 || formatCC == FORMAT_DXT3)
		options.SetTextureFormat(kTextureTypeTexture2D, kDXT3);
	else if (formatCC == FORMAT_DXT4 || formatCC == FORMAT_DXT5 || formatCC == FORMAT_RXGB)
		options.SetTextureFormat(kTextureTypeTexture2D, kDXT5);
	else
	{
		Warning("NvDXTlib : %s%s.dds - unsupported compression %s", file->path.c_str(), file->name.c_str(), getCompressionFormatString(formatCC));
		return false;
	}
	memset(&writeOptions, 0, sizeof(writeOptions));
	writeOptions.stream = stream;

	// compress
	NV_ERROR_CODE res = nvDDS::nvDXTcompress(Image_GetData(image), image->width,  image->height,  image->width*image->bpp, image->hasAlpha ? nvBGRA : nvBGR, &options, NvDXTLib_WriteDDS, 0);
	if (res == NV_OK)
	{
		for (MipMap *mipmap = image->mipMaps; mipmap; mipmap = mipmap->nextmip)
		{
			writeOptions.numwrites = 0;
			res = nvDDS::nvDXTcompress(mipmap->data, mipmap->width, mipmap->height,  mipmap->width*image->bpp, image->hasAlpha ? nvBGRA : nvBGR, &options, NvDXTLib_WriteDDS, 0);
			if (res != NV_OK)
				break;
		}
	}
	if (res != NV_OK)
	{
		Warning("NvDXTlib : %s%s.dds error - %s", file->path.c_str(), file->name.c_str(), getErrorString(res));
		return false;
	}
	return true;
}

/*
==========================================================================================

  DDS compression - NVidia Texture Tools Path

==========================================================================================
*/

class nvttOutputHandler : public nvtt::OutputHandler
{
public:
	byte *stream;
	void beginImage(int size, int width, int height, int depth, int face, int miplevel)
	{
	}
	bool writeData(const void *data, int size)
	{
		memcpy(stream, data, size);
		stream += size;
		return true;
	}
};

class nvttErrorHandler : public nvtt::ErrorHandler
{
public : 
	nvtt::Error errorCode;
	void error(nvtt::Error e)
	{
		errorCode = e;
	}
};

void NvTTlibCompressData(nvtt::InputOptions &inputOptions, nvtt::OutputOptions &outputOptions, byte *data, int width, int height, nvtt::CompressionOptions &compressionOptions)
{
	nvtt::Compressor compressor;

	inputOptions.setTextureLayout(nvtt::TextureType_2D, width, height);
	inputOptions.setMipmapData(data, width, height);
	compressor.process(inputOptions, compressionOptions, outputOptions);
}

bool NvTTlibCompress(byte *stream, FS_File *file, LoadedImage *image, DWORD formatCC)
{
	nvtt::CompressionOptions compressionOptions;
	nvtt::OutputOptions outputOptions;
	nvtt::InputOptions inputOptions;
	nvttErrorHandler errorHandler;
	nvttOutputHandler outputHandler;

	inputOptions.reset();
	compressionOptions.reset();
	outputOptions.reset();

	// options
	inputOptions.setMipmapGeneration(false);
	if (image->hasAlpha)
		inputOptions.setAlphaMode(nvtt::AlphaMode_Transparency);
	else
		inputOptions.setAlphaMode(nvtt::AlphaMode_None);
	if (image->useChannelWeighting)
		compressionOptions.setColorWeights(image->weightRed, image->weightGreen, image->weightBlue);
	if (formatCC == FORMAT_DXT1)
	{
		if (image->hasAlpha)
			compressionOptions.setFormat(nvtt::Format_DXT1a);
		else
			compressionOptions.setFormat(nvtt::Format_DXT1);
	}
	else if (formatCC == FORMAT_DXT2 || formatCC == FORMAT_DXT3)
		compressionOptions.setFormat(nvtt::Format_DXT3);
	else if (formatCC == FORMAT_DXT4 || formatCC == FORMAT_DXT5 || formatCC == FORMAT_RXGB)
		compressionOptions.setFormat(nvtt::Format_DXT5);
	else
	{
		Warning("NvTT : %s%s.dds - unsupported compression %s", file->path.c_str(), file->name.c_str(), getCompressionFormatString(formatCC));
		return false;
	}
	if (image->hasAlpha)
		compressionOptions.setPixelFormat(32, 0x0000FF, 0x00FF00, 0xFF0000, 0xFF000000);
	else
		compressionOptions.setPixelFormat(24, 0x0000FF, 0x00FF00, 0xFF0000, 0);
	compressionOptions.setQuality(nvtt::Quality_Production);
	
	// set output parameters
	outputOptions.setOutputHeader(false);
	outputOptions.setErrorHandler(&errorHandler);
	errorHandler.errorCode = nvtt::Error_Unknown;
	outputOptions.setOutputHandler(&outputHandler);
	outputHandler.stream = stream; 

	// write base texture and mipmaps
	NvTTlibCompressData(inputOptions, outputOptions, Image_GetData(image), image->width, image->height, compressionOptions);
	if (errorHandler.errorCode == nvtt::Error_Unknown)
	{
		for (MipMap *mipmap = image->mipMaps; mipmap; mipmap = mipmap->nextmip)
		{
			NvTTlibCompressData(inputOptions, outputOptions, mipmap->data, mipmap->width, mipmap->height, compressionOptions);
			if (errorHandler.errorCode != nvtt::Error_Unknown)
				break;
		}
	}
	
	// end, advance stats
	if (errorHandler.errorCode != nvtt::Error_Unknown)
	{
		Warning("NVTT Error: %s%s.dds - %s", file->path.c_str(), file->name.c_str(), nvtt::errorString(errorHandler.errorCode));
		return false;
	}
	return true;
}

/*
==========================================================================================

  DDS compression - ATI Compressor path

==========================================================================================
*/

// compress single image
ATI_TC_ERROR AtiCompressData(ATI_TC_Texture *src, ATI_TC_Texture *dst, byte *data, int width, int height, ATI_TC_CompressOptions *options, ATI_TC_FORMAT compressFormat, int bpp)
{

	// fill source pixels, swap rgb->bgr
	// premodulate color if requested
	src->dwWidth = width;
	src->dwHeight = height;
	src->dwPitch = width * bpp;
	src->dwDataSize = width * height * bpp;
	src->pData = data;
	
	// fill dst texture
	dst->dwWidth = width;
	dst->dwHeight = height;
	dst->dwPitch = 0;
	dst->format = compressFormat;
	dst->dwDataSize = ATI_TC_CalculateBufferSize(dst);

	// convert
	return ATI_TC_ConvertTexture(src, dst, options, NULL, NULL, NULL);
}

bool AtiCompress(byte *stream, FS_File *file, LoadedImage *image, DWORD formatCC)
{
	ATI_TC_Texture src;
	ATI_TC_Texture dst;
	ATI_TC_CompressOptions options;
	ATI_TC_FORMAT compress;

	memset(&src, 0, sizeof(src));
	memset(&options, 0, sizeof(options));

	// get options
	options.nAlphaThreshold = 127;
	options.nCompressionSpeed = ATI_TC_Speed_Normal;
	options.bUseAdaptiveWeighting = true;
	options.bUseChannelWeighting = false;
	options.dwSize = sizeof(options);
	if (image->useChannelWeighting)
	{
		options.bUseChannelWeighting = true;
		options.fWeightingRed = image->weightRed;
		options.fWeightingGreen = image->weightGreen;
		options.fWeightingBlue = image->weightBlue;
	}
	if (formatCC == FORMAT_DXT1)
	{
		if (image->hasAlpha)
			options.bDXT1UseAlpha = true;
		else
			options.bDXT1UseAlpha = false;
		compress = ATI_TC_FORMAT_DXT1;
	}
	else if (formatCC == FORMAT_DXT2 || formatCC == FORMAT_DXT3)
		compress = ATI_TC_FORMAT_DXT3;
	else if (formatCC == FORMAT_DXT4 || formatCC == FORMAT_DXT5 || formatCC == FORMAT_RXGB)
		compress = ATI_TC_FORMAT_DXT5;
	else
	{
		Warning("AtiCompress : %s%s.dds - unsupported compression %s", file->path.c_str(), file->name.c_str(), getCompressionFormatString(formatCC));
		return false;
	}

	// init source texture
	src.dwSize = sizeof(src);
	src.dwWidth = image->width;
	src.dwHeight = image->height;
	src.dwPitch = image->width*image->bpp;
	if (image->hasAlpha)
		src.format = ATI_TC_FORMAT_ARGB_8888;
	else
		src.format = ATI_TC_FORMAT_RGB_888;
	
	// init dest texture
	memset(&dst, 0, sizeof(dst));
	dst.dwSize = sizeof(dst);
	dst.dwWidth = image->width;
	dst.dwHeight = image->height;
	dst.dwPitch = 0;
	dst.format = compress;

	// compress
	ATI_TC_ERROR res = ATI_TC_OK;
	dst.pData = stream;
	res = AtiCompressData(&src, &dst, Image_GetData(image), image->width, image->height, &options, compress, image->bpp);
	if (res == ATI_TC_OK)
	{
		stream += dst.dwDataSize;
		// mipmaps
		for (MipMap *mipmap = image->mipMaps; mipmap; mipmap = mipmap->nextmip)
		{
			dst.pData = stream;
			res = AtiCompressData(&src, &dst, mipmap->data, mipmap->width, mipmap->height, &options, compress, image->bpp);
			if (res != ATI_TC_OK)
				break;
			stream += dst.dwDataSize;
		}
	}

	// end, advance stats
	if (res != ATI_TC_OK)
	{
		Warning("AtiCompress : %s%s.dds - compressor fail (error code %i)", file->path.c_str(), file->name.c_str(), res);
		return false;
	}
	return true;
}

/*
==========================================================================================

  DDS compression - Internal compressor

==========================================================================================
*/

bool InternalCompress(byte *stream, FS_File *file, LoadedImage *image, DWORD formatCC)
{
	if (formatCC == FORMAT_BGRA)
	{
		byte *in = Image_GetData(image);
		byte *end = in + image->width * image->height * image->bpp;
		byte *out = stream;

		if (image->hasAlpha)
		{
			while(in < end)
			{
				out[0] = in[0];
				out[1] = in[1];
				out[2] = in[2];
				out[3] = in[3];
				out += 4;		
				in  += 4;
			}
		}
		else
		{
			while(in < end)
			{
				out[0] = in[0];
				out[1] = in[1];
				out[2] = in[2];
				out[3] = 255;
				out += 4;
				in  += 3;
			}
		}
		return true;
	}
	Warning("InternalCompress : %s%s.dds - unsupported compression %s", file->path.c_str(), file->name.c_str(), getCompressionFormatString(formatCC));
	return false;
}

/*
==========================================================================================

  DDS compression - Generic

==========================================================================================
*/

void DDS_PrintModules(void)
{
	Print(" NVidia DDS Utilities %s\n", GetDXTCVersion());
	//Print(" NVidia Texture Tools  %i.%i\n", (int)(nvtt::version() / 100), nvtt::version() - ((int)(nvtt::version()) / 100)*100);
	Print(" ATI Compress %i.%i\n", ATI_COMPRESS_VERSION_MAJOR, ATI_COMPRESS_VERSION_MINOR);
}

byte *GenerateDDS(FS_File *file, LoadedImage *image, size_t *outdatasize)
{
	bool isNormalmap;
	bool isHeightmap;
	COLORSWIZZLE colorSwizzle;
	DWORD formatCC;
	TOOL tool;
	bool res;

	if (!image->bitmap)
		return NULL;

	// detect special texture types
	isNormalmap = FS_FileMatchList(file, opt_isNormal);
	isHeightmap = FS_FileMatchList(file, opt_isHeight);

	// select compression type
	colorSwizzle = IMAGE_COLORSWIZZLE_NONE;
	if (isHeightmap)
		formatCC = FORMAT_DXT1;
	if (isNormalmap && opt_normalmapRXGB)
		formatCC = FORMAT_RXGB;
	else if (image->hasAlpha)
		formatCC = image->hasGradientAlpha ? FORMAT_DXT5 : FORMAT_DXT1;
	else
		formatCC = FORMAT_DXT1;

	if (opt_forceAllFormat)
		formatCC = opt_forceAllFormat;
	else if (FS_FileMatchList(file, opt_forceDXT1))
		formatCC = FORMAT_DXT1;
	else if (FS_FileMatchList(file, opt_forceDXT2))
		formatCC = FORMAT_DXT2;
	else if (FS_FileMatchList(file, opt_forceDXT3))
		formatCC = FORMAT_DXT3;
	else if (FS_FileMatchList(file, opt_forceDXT4))
		formatCC = FORMAT_DXT4;
	else if (FS_FileMatchList(file, opt_forceDXT5))
		formatCC = FORMAT_DXT5;
	else if (FS_FileMatchList(file, opt_forceBGRA))
		formatCC = FORMAT_BGRA;
	else if (FS_FileMatchList(file, opt_forceRXGB))
		formatCC = FORMAT_RXGB;
	if (formatCC == FORMAT_DXT4 || formatCC == FORMAT_DXT2)
		colorSwizzle = IMAGE_COLORSWIZZLE_PREMODULATE;

	// check if selected DXT3/DXT5 and image have no alpha information
	if (!image->hasAlpha)
		if (formatCC == FORMAT_DXT3 || formatCC == FORMAT_DXT5)
			formatCC = FORMAT_DXT1;

	// check for swizzled format
	if (formatCC == FORMAT_RXGB)
		colorSwizzle = IMAGE_COLORSWIZZLE_XGBR;

	// select compressor tool
	tool = opt_compressor;
	if (tool == COMPRESSOR_AUTOSELECT)
	{
		// hybrid mode, pick best
		// NVidia tool compresses normalmaps better than ATI
		// while ATI wins over general DXT1 and DXT5
		if (isNormalmap)
			tool = COMPRESSOR_NVIDIA;
		else
			tool = COMPRESSOR_ATI;
		// check if forcing
		if (FS_FileMatchList(file, opt_forceNvCompressor))
			tool = COMPRESSOR_NVIDIA;
		else if (FS_FileMatchList(file, opt_forceATICompressor))
			tool = COMPRESSOR_ATI;
	}
	if (formatCC == FORMAT_BGRA)
		tool = COMPRESSOR_INTERNAL;

	// for AI compressor we set color weiting (NVidia compressor has nice automatic weighting)
	if (tool == COMPRESSOR_ATI)
	{
		if (formatCC == FORMAT_RXGB)
		{
			image->useChannelWeighting = true;
			image->weightRed = 0.0;
			image->weightGreen = 0.75;
			image->weightBlue = 0.25;
		}
		else if (isNormalmap)
		{
			image->useChannelWeighting = true;
			image->weightRed = 0.5f;
			image->weightGreen = 0.5f;
			image->weightBlue = 0.0f;
		}
	}

	// postprocess
	Image_ConvertColorsForCompression(image, true, colorSwizzle); // DDS compressors expects BGR
	if (FS_FileMatchList(file, opt_scale) || opt_forceScale2x)
		Image_Scale2x(image, opt_scaler, opt_allowNonPowerOfTwoDDS ? false : true);
	if (!opt_allowNonPowerOfTwoDDS)
		Image_MakePowerOfTwo(image);
	if (formatCC == FORMAT_DXT1 && image->hasAlpha)
		Image_MakeAlphaBinary(image, 180);
	if (!opt_forceNoMipmaps && !FS_FileMatchList(file, opt_nomip))
		Image_GenerateMipmaps(image);

	// allocate memory for destination DDS
	image->formatCC = formatCC;
	size_t datasize = CompressedSize(image, formatCC, true, true, true);
	byte  *data = (byte *)mem_alloc(datasize);
	memset(data, 0, datasize);
	byte  *in = DDSHeader(data, image, formatCC);

	// compress
	if (tool == COMPRESSOR_ATI)
		res = AtiCompress(in, file, image, formatCC);
	else if (tool == COMPRESSOR_NVIDIA)
		res = NvDXTlibCompress(in, file, image, formatCC);
	else if (tool == COMPRESSOR_NVIDIA_TT)
		res = NvTTlibCompress(in, file, image, formatCC);
	else if (tool == COMPRESSOR_INTERNAL)
		res = InternalCompress(in, file, image, formatCC);
	else
		Error("bad compression tool!\n");

	*outdatasize = datasize;
	return data;
}

typedef struct writeDDS_s
{
	char        outfile[MAX_FPATH];
	byte       *data;
	size_t      datasize;
	writeDDS_s *next;
} writeDDS_t;

typedef struct
{
	// stats
	size_t     num_dds_files;
	double     size_dds_headerdata;
	double     size_dds_texturedata;
	double     size_dds_mipdata;
	size_t     num_original_files;
	double     size_original_textures;
	double     size_original_files;

	// zip file in memory
	void *zip_data;
	size_t zip_len;
	size_t zip_maxlen;
	bool zip_warning;

	// write chain
	writeDDS_t *writeDDS;
	int num_write_files;
	int num_writen_files;
} GenerateDDS_t;


void GenerateDDS_Thread(ThreadData *thread)
{
	LoadedImage *image, *frame;
	GenerateDDS_t *SharedData;
	writeDDS_t *WriteData;
	FS_File *file;
	byte *data;
	size_t datasize;
	int work;

	SharedData = (GenerateDDS_t *)thread->data;

	image = Image_Create();
	while(1)
	{
		work = GetWorkForThread(thread);
		if (work == -1)
			break;

		file = &textures[work];

		// load image
		Image_Load(file, image);
		if (!image->bitmap)
			continue;

		// postprocess and export all frames
		double dds_texsize  = 0;
		double dds_mipsize  = 0;
		double dds_headsize = 0;
		double org_filesize = 0;
		double org_texsize  = 0;
		size_t numdds    = 0;
		for (frame = image; frame != NULL; frame = frame->next)
		{
			data = GenerateDDS(file, frame, &datasize);

			// save for saving thread
			WriteData = (writeDDS_t *)mem_alloc(sizeof(writeDDS_t));
			memset(WriteData, 0, sizeof(writeDDS_t));
			sprintf(WriteData->outfile, "%s%s.dds", file->path.c_str(), frame->useTexname ? frame->texname : file->name.c_str());
			WriteData->data = data;
			WriteData->datasize = datasize;
			WriteData->next = SharedData->writeDDS;
			SharedData->writeDDS = WriteData;
			SharedData->num_write_files++;
			
			// stats
			dds_headsize += (double)CompressedSize(frame, frame->formatCC,  true, false, false) / (1024.0f * 1024.0f);
			dds_texsize  += (double)CompressedSize(frame, frame->formatCC, false,  true, false) / (1024.0f * 1024.0f);
			dds_mipsize  += (double)CompressedSize(frame, frame->formatCC, false, false,  true) / (1024.0f * 1024.0f); 
			org_filesize += (double)frame->filesize / (1024.0f * 1024.0f);
			org_texsize  += (double)((frame->width*frame->height*frame->bpp)/frame->scale) / (1024.0f * 1024.0f);
			numdds++;
		}
		
		// add to global stats
		SharedData->num_original_files++;
		SharedData->size_original_files    += org_filesize;
		SharedData->size_original_textures += org_texsize;
		SharedData->num_dds_files          += numdds;
		SharedData->size_dds_headerdata    += dds_headsize;
		SharedData->size_dds_texturedata   += dds_texsize;
		SharedData->size_dds_mipdata       += dds_mipsize;

		// delete image
		Image_Unload(image);
	}
	Image_Delete(image);
}

void GenerateDDS_MainThread(ThreadData *thread)
{
	HZIP outzip = NULL;
	GenerateDDS_t *SharedData;
	writeDDS_t *WriteData, *PrevData;
	void *zipdata;
	unsigned long zipdatalen;
	char outfile[MAX_FPATH];

	SharedData = (GenerateDDS_t *)thread->data;

	// check if dest path is an archive
	if (!FS_FileMatchList(opt_destPath, opt_archiveFiles))
	{
		Print("Generating to \"%s\"\n", opt_destPath);
		AddSlash(opt_destPath);
	}
	else
	{
		if (opt_zipMemory <= 0)
			outzip = CreateZip(opt_destPath, "");
		else
		{
			SharedData->zip_maxlen = 1024 * 1024 * opt_zipMemory;
			SharedData->zip_data = mem_alloc(SharedData->zip_maxlen);
			outzip = CreateZip(SharedData->zip_data, SharedData->zip_maxlen, "");
		}
		if (!outzip)
		{
			thread->pool->stop = true;
			Print("Failed to create output archive file %s\n", opt_destPath);
			return;
		}
		Print("Generating to \"%s\" (ZIP archive)\n", opt_destPath);
		if (opt_zipMemory > 0)
			Print("Generating ZIP in memory (max size %i MBytes)\n", opt_zipMemory);
		if (opt_archivePath.c_str()[0])
			Print("Archive inner path \"%s\"\n", opt_archivePath.c_str());
	}
	thread->pool->started = true;

	// write files
	while(1)
	{
		// print pacifier
		if (!noprint)
			Pacifier(" file %i of %i", SharedData->num_original_files, thread->pool->work_num /*, SharedData->num_write_files, SharedData->num_writen_files*/);
		else
		{
			int p = (int)(((float)(SharedData->num_original_files) / (float)thread->pool->work_num)*100);
			PercentPacifier("%i", p);
		}

		// check if files to write, thats because they havent been added it, or we are finished
		if (!SharedData->writeDDS)
		{
			if (thread->pool->finished == true)
				break;
			// todo: do some help?
			Sleep(1);
		}
		else
		{
			// get a last file from chain
			PrevData = NULL;
			for (WriteData = SharedData->writeDDS; WriteData->next; WriteData = WriteData->next)
				PrevData = WriteData;
			if (!PrevData)
			{
				WriteData = SharedData->writeDDS;
				SharedData->writeDDS = NULL;
			}
			else
			{
				WriteData = PrevData->next;
				PrevData->next = NULL;
			}
			SharedData->num_write_files--;
			SharedData->num_writen_files++;

			// write
			if (outzip)
			{
				sprintf(outfile, "%s%s", opt_archivePath.c_str(), WriteData->outfile);
				zipdatalen = ZipGetMemoryWritten(outzip);
				if (!SharedData->zip_warning && (float)((float)zipdatalen / (float)SharedData->zip_maxlen) > 0.9)
				{

					Warning("GenerateDDS: there is less than 10%% of free ZIP memory, program may crash!\n");
					SharedData->zip_warning = true;
				}
				ZRESULT zr = ZipAdd(outzip, outfile, WriteData->data, WriteData->datasize);
				if (zr != ZR_OK)
					Warning("GenerateDDS(%s): failed to pack DDS into ZIP - error code 0x%08X", outfile, zr);
			}
			else
			{
				sprintf(outfile, "%s%s", opt_destPath, WriteData->outfile);
				// write file
				CreatePath(outfile);
				FILE *f = fopen(outfile, "wb");
				if (!f)
					Warning("GenerateDDS(%s): cannot open file (%s) for writing", outfile, strerror(errno));
				else
				{
					if (!fwrite(WriteData->data, WriteData->datasize, 1, f))
						Warning("GenerateDDS(%s): cannot write file (%s)", outfile, strerror(errno));
					fclose(f);
				}
			}
			mem_free(WriteData->data);
			mem_free(WriteData);
		}
	}

	// close zip
	if (outzip)
	{
		if (SharedData->zip_data)
		{
			ZRESULT zr = ZipGetMemory(outzip, &zipdata, &zipdatalen);
			if (zr != ZR_OK)
				Warning("GenerateDDS(%s): ZipGetMemory failed - error code 0x%08X", outfile, zr);
			else
			{
				FILE *f = fopen(opt_destPath, "wb");
				if (!f)
					Warning("GenerateDDS(%s): cannot open ZIP file (%s) for writing", opt_destPath, strerror(errno));
				else
				{
					if (!fwrite(zipdata, zipdatalen, 1, f))
						Warning("GenerateDDS(%s): cannot write ZIP file (%s)", opt_destPath, strerror(errno));
					fclose(f);
				}
				mem_free(SharedData->zip_data);
			}
		}
		CloseZip(outzip);
	}
}

void DDS_Help(void)
{
	waitforkey = true;
	Print(
	"usage: rwgtex -dds [input path] [output path] [options]\n"
	"\n"
	"input path could be:\n"
	"  - folder\n"
	"  - a single file or file mask\n"
	"  - archive file (extension should be listed in option file)\n"
	"\n"
	"output path:\n"
	"  - folder\n"
	"  - archive (will be created from scratch)\n"
	"\n"
	"options:\n"
	"        -ap: additional archive path\n"
	"   -nocache: disable file caching\n"
	"      -npot: allow non-power-of-two textures\n"
	"     -nomip: dont generate mipmaps\n"
	"        -nv: force NVidia DDS Utilities compressor\n"
	"       -ati: force ATI compressor\n"
	"      -dxt1: force DXT1 compression\n"
	"      -dxt2: force DXT2 compression\n"
	"      -dxt3: force DXT3 compression\n"
	"      -dxt4: force DXT4 compression\n"
	"      -dxt5: force DXT5 compression\n"
	"      -bgra: force BGRA format (no compression)\n"
	"        -2x: apply 2x scale (Scale2X)\n"
	"  -scaler X: set a filter to be used for scaling\n"
	"  -zipmem X: keep zip file in memory until end\n"
	"             this avoids many file writes\n"
	"\n"
	"scalers:\n"
	"        box: nearest\n"
	"   bilinear: bilinear filter\n"
	"    bicubic: Mitchell & Netravali's two-param cubic filter\n"
	"    bspline: th order (cubic) b-spline\n"
	" catmullrom: Catmull-Rom spline, Overhauser spline\n"
	"    lanczos: Lanczos3 filter\n"
	"    scale2x: Scale2x\n"
	"    super2x: Scale4x with backscale to 2x (default)\n"
	"\n");
}

int DDS_Main(int argc, char **argv)
{
	double timeelapsed;
	char cachefile[MAX_FPATH];
	int i;

	// launched without parms, try to find kain.exe
	i = 0;
	strcpy(opt_srcDir, "");
	strcpy(opt_srcFile, "");
	strcpy(opt_destPath, "");
	if (argc < 1)
	{
		// launched without parms, try to find engine
		char find[MAX_FPATH];
		bool found_dir = false;
		if (opt_basedir.c_str()[0])
		{
			for (int i = 0; i < 10; i++)
			{
				strcpy(find, "../");
				for (int j = 0; j < i; j++)
					strcat(find, "../");
				strcat(find, opt_basedir.c_str());

				{
					found_dir = true;
					break;
				}
			}
		}
		if (found_dir)
		{
			Print("Base and output directory detected\n");
			sprintf(opt_srcDir, "%s/", find);
			sprintf(opt_destPath, "%s/%s/", find, opt_ddsdir.c_str());
		}
		else
		{
			DDS_Help();
			Error("no commands specified", progname);
		}
	}
	else if (argc < 2)
	{
		// dragged file to exe, there is no output path
		strncpy(opt_srcDir, argv[0], sizeof(opt_srcDir));
		opt_useFileCache = false;
		if (FS_FindDir(opt_srcDir)) 
		{
			// dragged a directory
			AddSlash(opt_srcDir);
			sprintf(opt_destPath, "%s%s", opt_srcDir, opt_ddsdir.c_str());
		}
		else
		{
			// dragged a file
			ExtractFileName(opt_srcDir, opt_srcFile);
			ExtractFilePath(opt_srcDir, opt_destPath);
			strncpy(opt_srcDir, opt_destPath, sizeof(opt_srcDir));
			// if file is archive, add "dds/" folder
			if (FS_FileMatchList(opt_srcFile, opt_archiveFiles))
				strcat(opt_destPath, opt_ddsdir.c_str());
		}
	}
	else
	{
		// commandline launch
		strncpy(opt_srcDir, argv[0], sizeof(opt_srcDir));

		// optional output directory
		if (argc > 1 && strncmp(argv[1], "-", 1))
			strncpy(opt_destPath, argv[1], sizeof(opt_destPath));

		// check if input is folder
		// set default output directory
		if (FS_FindDir(opt_srcDir))
			AddSlash(opt_srcDir);
		else
		{
			ExtractFileName(opt_srcDir, opt_srcFile);
			ExtractFilePath(opt_srcDir, opt_srcDir);
		}

		// default output directory
		if (!strcmp(opt_destPath, ""))
		{
			AddSlash(opt_srcDir);
			sprintf(opt_destPath, "%s%s", opt_srcDir, opt_ddsdir.c_str());
		}
	}
	AddSlash(opt_srcDir);

	// print options
	if (opt_compressor == COMPRESSOR_NVIDIA)
		Print("Using NVidia DDS Utilities compressor\n");
	else if (opt_compressor == COMPRESSOR_NVIDIA_TT)
		Print("Using NVidia Texture Tools compressor\n");
	else if (opt_compressor == COMPRESSOR_ATI)
		Print("Using ATI compressor\n");
	else
		Print("Using hybrid (autoselect) compressor\n");
	if (opt_forceAllFormat)
	{
		if (opt_forceAllFormat == FORMAT_DXT1)
			Print("Forcing DDS format to DXT1\n");
		else if (opt_forceAllFormat == FORMAT_DXT3)
			Print("Forcing DDS format to DXT3\n");
		else if (opt_forceAllFormat == FORMAT_DXT5)
			Print("Forcing DDS format to DXT5\n");
		else if (opt_forceAllFormat == FORMAT_BGRA)
			Print("Forcing DDS format to BGRA\n");
		else if (opt_forceAllFormat == FORMAT_RXGB)
			Print("Forcing DDS format to RXGB (Doom 3 DXT5 swizzled normalmap format)\n");
	}	
	if (opt_forceAllNormalmap)
		Print("Forcing normalmap compression tricks\n");
	if (opt_forceScale2x)
	{
		Print("Upscale images to 200%% ");
		if (opt_scaler == IMAGE_SCALER_BOX)
			Print("using box filter\n");
		else if (opt_scaler == IMAGE_SCALER_BILINEAR)
			Print("using bilinear filter\n");
		else if (opt_scaler == IMAGE_SCALER_BICUBIC)
			Print("using cubic Mitchell filter\n");
		else if (opt_scaler == IMAGE_SCALER_BSPLINE)
			Print("using cubic b-spline\n");
		else if (opt_scaler == IMAGE_SCALER_CATMULLROM)
			Print("using Catmull-Rom spline\n");
		else if (opt_scaler == IMAGE_SCALER_LANCZOS)
			Print("using Lanczos filter\n");
		else if (opt_scaler == IMAGE_SCALER_SCALE2X)
			Print("using Scale2x filter\n");
		else
			Print("using Super2x filter\n");
	}
	if (opt_allowNonPowerOfTwoDDS)
		Print("Allow non-power-of-two texture dimensions\n");
	if (opt_forceNoMipmaps)
		 Print("Not generating mipmaps\n");

	// load cache
	if (opt_useFileCache)
	{
		// check if dest path is an archive
		if (FS_FileMatchList(opt_destPath, opt_archiveFiles))
		{
			opt_useFileCache = false;
			Print("Archive output does not support file cache at the moment\n");
		}
		else
		{
			Print("Converting only files that was changed\n");
			sprintf(cachefile, "%s_filescrc.txt", opt_destPath);
			FS_LoadCache(cachefile);
		}
	}

	// find files
	Print("Entering \"%s%s\"\n", opt_srcDir, opt_srcFile);
	if (opt_useFileCache)
		Print("Calculating crc32 for files\n");
	textures.clear();
	texturesSkipped = 0;
	FS_ScanPath(opt_srcDir, opt_srcFile, NULL);
	if (texturesSkipped)
		Print("Skipping %i unchanged files\n", texturesSkipped);
	if (!textures.size())
	{
		Print("No files to convert\n");
		return 0;
	}

	// decompress DDS
	if (opt_srcFile[0] && textures.size() == 1 && !strnicmp(textures[0].suf.c_str(), "dds", 3))
	{
		Print("Decompressing DDS\n");
		Print("Not implemented\n");
		return 0;
	}

	// run DDS conversion
	GenerateDDS_t SharedData;
	memset(&SharedData, 0, sizeof(GenerateDDS_t));
	timeelapsed = ParallelThreads(numthreads, textures.size(), &SharedData, GenerateDDS_Thread, GenerateDDS_MainThread);

	// save cache file
	if (opt_useFileCache)
		FS_SaveCache(cachefile);

	// show stats
	double outputsize = SharedData.size_dds_headerdata + SharedData.size_dds_mipdata + SharedData.size_dds_texturedata;
	Print("Conversion finished!\n");
	Print("--------\n");
	Print("  files exported: %i\n", SharedData.num_dds_files);
	Print("    time elapsed: %i:%02.0f\n", (int)(timeelapsed / 60), (timeelapsed - ((int)(timeelapsed / 60)*60)));
	Print("     input files: %.2f mb\n", SharedData.size_original_files);
	Print("  input textures: %.2f mb\n", SharedData.size_original_textures);
	Print("       DDS files: %.2f mb\n", SharedData.size_dds_headerdata + SharedData.size_dds_mipdata + SharedData.size_dds_texturedata);
	Print("     inc mipmaps: %.2f mb\n", SharedData.size_dds_mipdata);
	Print("   space economy: %.0f%%\n", (1.0 - (outputsize / SharedData.size_original_files)) * 100.0f);
	Print("    VRAM economy: %.0f%%\n", (1.0 - (SharedData.size_dds_texturedata / SharedData.size_original_textures)) * 100.0f);
	return 0;
}