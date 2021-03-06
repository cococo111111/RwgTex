////////////////////////////////////////////////////////////////
//
// RwgTex / BGRA encoder
// (c) Pavel [VorteX] Timofeyev
// See LICENSE text file for a license agreement
//
////////////////////////////////

#include "main.h"
#include "tex.h"

TexTool TOOL_RWGTP =
{
	"RWGTP", "RwgTex Packer", "rwgtp",
	TEXINPUT_BGR | TEXINPUT_BGRA,
	&ToolRWGTP_Init,
	&ToolRWGTP_Option,
	&ToolRWGTP_Load,
	&ToolRWGTP_Compress,
	&ToolRWGTP_Version,
};

/*
==========================================================================================

  Init

==========================================================================================
*/

void ToolRWGTP_Init(void)
{
	RegisterFormat(&F_BGRA, &TOOL_RWGTP);
	RegisterFormat(&F_BGR6, &TOOL_RWGTP);
	RegisterFormat(&F_BGR3, &TOOL_RWGTP);
	RegisterFormat(&F_BGR1, &TOOL_RWGTP);
}

void ToolRWGTP_Option(const char *group, const char *key, const char *val, const char *filename, int linenum)
{
}

void ToolRWGTP_Load(void)
{
}

const char *ToolRWGTP_Version(void)
{
	static char versionstring[200];
	sprintf(versionstring, "1.0");
	return versionstring;
}

/*
==========================================================================================

  Compression

==========================================================================================
*/

size_t PackBGRAData(TexEncodeTask *t, byte *stream, byte *data, int width, int height)
{
	if (t->format->block == &B_BGRA || t->format->block == &B_BGR6 || t->format->block == &B_BGR3 || t->format->block == &B_BGR1)
	{
		byte *in = data;
		byte *end = in + width * height * t->image->bpp;
		byte *out = stream;
		int bpp = t->format->block->bitlength / 8;
		if (t->image->hasAlpha)
		{
			while(in < end)
			{
				out[0] = in[0];
				out[1] = in[1];
				out[2] = in[2];
				out[3] = in[3];
				out += bpp;		
				in  += t->image->bpp;
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
				out += bpp;
				in  += t->image->bpp;
			}
		}
		return width * height * ( t->format->block->bitlength / 8);
	}
	Warning("PackBGRA : %s%s.dds - unsupported compression %s/%s", t->file->path.c_str(), t->file->name.c_str(), t->format->name, t->format->block->name);
	return false;
}

bool ToolRWGTP_Compress(TexEncodeTask *t)
{
	byte *stream = t->stream;
	for (ImageMap *map = t->image->maps; map; map = map->next)
		stream += PackBGRAData(t, stream, map->data, map->width, map->height);
	return true;
}