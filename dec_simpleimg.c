/*
 *			GPAC - Multimedia Framework C SDK
 *
 *  This file is part of GPAC / simple bitmap decoder filter, covering PCX,
 *  TGA, SGI/RGB, PNM (PBM/PGM/PPM) and XBM.
 *
 *  Unlike the other image filters here this one has no third-party library
 *  behind it: each of these formats is a header plus either raw samples or a
 *  byte-oriented run-length scheme, so the decoders are written in-tree, in
 *  simpleimg.c. That file has no GPAC or emscripten dependency, which is what
 *  lets it be compiled natively and checked against an independent reader.
 *
 *  The format is chosen from the file extension, as several of these have no
 *  usable magic number at all (XBM is C source, PCX's single 0x0A byte is not
 *  distinctive).
 */

#include <gpac/filters.h>
#include <gpac/constants.h>
#include <string.h>
#include <stdlib.h>

#include "simpleimg.h"

typedef struct
{
	GF_FilterPid *ipid, *opid;
	Bool is_playing;
	SimpleImgFormat format;
	Bool has_format;
} GF_SimpleImgDecCtx;

static GF_Err simpleimgdec_configure_pid(GF_Filter *filter, GF_FilterPid *pid, Bool is_remove)
{
	const GF_PropertyValue *p;
	GF_SimpleImgDecCtx *ctx = (GF_SimpleImgDecCtx *)gf_filter_get_udta(filter);

	if (is_remove)
	{
		if (ctx->opid)
		{
			gf_filter_pid_remove(ctx->opid);
			ctx->opid = NULL;
		}
		ctx->ipid = NULL;
		return GF_OK;
	}
	if (!gf_filter_pid_check_caps(pid))
		return GF_NOT_SUPPORTED;

	ctx->ipid = pid;
	gf_filter_pid_set_framing_mode(pid, GF_TRUE);

	/* Resolve the format here rather than in process: the extension is what
	 * selects the decoder, and knowing it early keeps the failure - an
	 * extension we do not serve - out of the data path. */
	ctx->has_format = GF_FALSE;
	p = gf_filter_pid_get_property(pid, GF_PROP_PID_FILE_EXT);
	if (p && p->value.string && simpleimg_format_from_ext(p->value.string, &ctx->format))
		ctx->has_format = GF_TRUE;

	if (!ctx->opid)
		ctx->opid = gf_filter_pid_new(filter);

	gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_STREAM_TYPE, &PROP_UINT(GF_STREAM_VISUAL));
	gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_CODECID, &PROP_UINT(GF_CODECID_RAW));
	gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_PIXFMT, &PROP_UINT(GF_PIXEL_RGB));

	return GF_OK;
}

static Bool simpleimgdec_process_event(GF_Filter *filter, const GF_FilterEvent *evt)
{
	GF_SimpleImgDecCtx *ctx = (GF_SimpleImgDecCtx *)gf_filter_get_udta(filter);
	switch (evt->base.type)
	{
	case GF_FEVT_PLAY:
		ctx->is_playing = GF_TRUE;
		return GF_FALSE;
	case GF_FEVT_STOP:
		ctx->is_playing = GF_FALSE;
		return GF_FALSE;
	default:
		return GF_FALSE;
	}
}

static GF_Err simpleimgdec_process(GF_Filter *filter)
{
	GF_FilterPacket *pck, *dst_pck;
	u8 *data, *output;
	u32 size;
	unsigned char *rgb = NULL;
	unsigned int width = 0, height = 0;
	int res;
	GF_SimpleImgDecCtx *ctx = (GF_SimpleImgDecCtx *)gf_filter_get_udta(filter);

	pck = gf_filter_pid_get_packet(ctx->ipid);
	if (!pck)
	{
		if (gf_filter_pid_is_eos(ctx->ipid))
		{
			gf_filter_pid_set_eos(ctx->opid);
			return GF_EOS;
		}
		return GF_OK;
	}
	data = (u8 *)gf_filter_pck_get_data(pck, &size);
	if (!data)
	{
		gf_filter_pid_drop_packet(ctx->ipid);
		return GF_IO_ERR;
	}
	if (!ctx->has_format)
	{
		gf_filter_pid_drop_packet(ctx->ipid);
		GF_LOG(GF_LOG_ERROR, GF_LOG_CODEC, ("[SimpleImgDec] No file extension to select a decoder from\n"));
		return GF_NOT_SUPPORTED;
	}

	res = simpleimg_decode(ctx->format, data, size, &rgb, &width, &height);
	gf_filter_pid_drop_packet(ctx->ipid);

	switch (res)
	{
	case SIMPLEIMG_OK:
		break;
	case SIMPLEIMG_ERR_MEMORY:
		return GF_OUT_OF_MEM;
	case SIMPLEIMG_ERR_VARIANT:
		GF_LOG(GF_LOG_ERROR, GF_LOG_CODEC, ("[SimpleImgDec] Unsupported variant of this format\n"));
		return GF_NOT_SUPPORTED;
	default:
		GF_LOG(GF_LOG_ERROR, GF_LOG_CODEC, ("[SimpleImgDec] Truncated or malformed file\n"));
		return GF_NON_COMPLIANT_BITSTREAM;
	}

	gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_WIDTH, &PROP_UINT(width));
	gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_HEIGHT, &PROP_UINT(height));
	gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_STRIDE, &PROP_UINT(width * 3));
	gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_PIXFMT, &PROP_UINT(GF_PIXEL_RGB));

	dst_pck = gf_filter_pck_new_alloc(ctx->opid, width * height * 3, &output);
	if (!dst_pck)
	{
		simpleimg_free(rgb);
		return GF_OUT_OF_MEM;
	}
	memcpy(output, rgb, width * height * 3);
	simpleimg_free(rgb);

	gf_filter_pck_set_cts(dst_pck, 0);
	gf_filter_pck_set_sap(dst_pck, GF_FILTER_SAP_1);
	gf_filter_pck_send(dst_pck);

	gf_filter_pid_set_eos(ctx->opid);
	return GF_EOS;
}

static void simpleimgdec_finalize(GF_Filter *filter)
{
}

static const GF_FilterCapability SimpleImgDecCaps[] =
	{
		CAP_UINT(GF_CAPS_INPUT, GF_PROP_PID_STREAM_TYPE, GF_STREAM_FILE),
		CAP_STRING(GF_CAPS_INPUT, GF_PROP_PID_FILE_EXT, "pcx|tga|targa|icb|vda|vst|sgi|rgb|rgba|bw|int|inta|pnm|ppm|pgm|pbm|xbm"),
		CAP_STRING(GF_CAPS_INPUT, GF_PROP_PID_MIME, "image/x-pcx|image/x-targa|image/x-tga|image/sgi|image/x-sgi-rgb|image/x-portable-anymap|image/x-portable-pixmap|image/x-portable-graymap|image/x-portable-bitmap|image/x-xbitmap"),
		CAP_UINT(GF_CAPS_OUTPUT, GF_PROP_PID_STREAM_TYPE, GF_STREAM_VISUAL),
		CAP_UINT(GF_CAPS_OUTPUT, GF_PROP_PID_CODECID, GF_CODECID_RAW),
};

GF_FilterRegister SimpleImgDecoderRegister = {
	.name = "simpleimgdec",
	GF_FS_SET_DESCRIPTION("Simple bitmap decoder (PCX, TGA, SGI, PNM, XBM)")
		GF_FS_SET_HELP("This filter decodes PCX, Truevision TGA, SGI/RGB, PNM (PBM/PGM/PPM) and X11 XBM images. The decoders are built into the filter; these formats have no third-party library behind them.")
			.private_size = sizeof(GF_SimpleImgDecCtx),
	SETCAPS(SimpleImgDecCaps),
	.configure_pid = simpleimgdec_configure_pid,
	.process = simpleimgdec_process,
	.process_event = simpleimgdec_process_event,
	.finalize = simpleimgdec_finalize,
};

const GF_FilterRegister *EMSCRIPTEN_KEEPALIVE simpleimgdec_register(GF_FilterSession *session)
{
	return &SimpleImgDecoderRegister;
}

#include "filter_register.h"
__attribute__((constructor))
void register_simpleimgdec(void) {
    gf_filter_auto_register("simpleimgdec", simpleimgdec_register);
}
