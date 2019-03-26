#ifndef CC_ERRORS_H
#define CC_ERRORS_H
/* Represents list of internal errors.
   Copyright 2017 ClassicalSharp | Licensed under BSD-3
*/

/* NOTE: When changing these, remember to keep Logger.C up to date! */
enum ERRORS_ALL {
	ERROR_BASE = 0xCCDED, // TODO / NOTE / FIXME
	ERR_END_OF_STREAM,

	/* Ogg stream decoding errors */
	OGG_ERR_INVALID_SIG, OGG_ERR_VERSION,
	/* WAV audio decoding errors*/
	WAV_ERR_STREAM_HDR, WAV_ERR_STREAM_TYPE, WAV_ERR_DATA_TYPE, WAV_ERR_NO_DATA,
	/* Vorbis audio decoding errors */
	VORBIS_ERR_HEADER,
	VORBIS_ERR_WRONG_HEADER, VORBIS_ERR_FRAMING,
	VORBIS_ERR_VERSION, VORBIS_ERR_BLOCKSIZE, VORBIS_ERR_CHANS,
	VORBIS_ERR_TIME_TYPE, VORBIS_ERR_FLOOR_TYPE, VORBIS_ERR_RESIDUE_TYPE,
	VORBIS_ERR_MAPPING_TYPE, VORBIS_ERR_MODE_TYPE,
	VORBIS_ERR_CODEBOOK_SYNC, VORBIS_ERR_CODEBOOK_ENTRY, VORBIS_ERR_CODEBOOK_LOOKUP,
	VORBIS_ERR_MODE_WINDOW, VORBIS_ERR_MODE_TRANSFORM,
	VORBIS_ERR_MAPPING_CHANS, VORBIS_ERR_MAPPING_RESERVED,
	VORBIS_ERR_FRAME_TYPE,
	/* PNG image decoding errors */
	PNG_ERR_INVALID_SIG, PNG_ERR_INVALID_HDR_SIZE, PNG_ERR_TOO_WIDE, PNG_ERR_TOO_TALL,
	PNG_ERR_INVALID_COL_BPP, PNG_ERR_COMP_METHOD, PNG_ERR_FILTER, PNG_ERR_INTERLACED,
	PNG_ERR_PAL_SIZE, PNG_ERR_TRANS_COUNT, PNG_ERR_TRANS_INVALID, PNG_ERR_INVALID_END_SIZE, PNG_ERR_NO_DATA,
	/* ZIP archive decoding errors */
	ZIP_ERR_TOO_MANY_ENTRIES, ZIP_ERR_SEEK_END_OF_CENTRAL_DIR, ZIP_ERR_NO_END_OF_CENTRAL_DIR,
	ZIP_ERR_SEEK_CENTRAL_DIR, ZIP_ERR_INVALID_CENTRAL_DIR,
	ZIP_ERR_SEEK_LOCAL_DIR, ZIP_ERR_INVALID_LOCAL_DIR, ZIP_ERR_FILENAME_LEN,
	/* GZIP header decoding errors */
	GZIP_ERR_HEADER1, GZIP_ERR_HEADER2, GZIP_ERR_METHOD, GZIP_ERR_FLAGS,
	/* ZLIB header decoding errors */
	ZLIB_ERR_METHOD, ZLIB_ERR_WINDOW_SIZE, ZLIB_ERR_FLAGS,
	/* FCM map decoding errors */
	FCM_ERR_IDENTIFIER, FCM_ERR_REVISION,
	/* LVL map decoding errors */
	LVL_ERR_VERSION,
	/* DAT map decoding errors */
	DAT_ERR_IDENTIFIER, DAT_ERR_VERSION, DAT_ERR_JIDENTIFIER, DAT_ERR_JVERSION,
	DAT_ERR_ROOT_TYPE, DAT_ERR_JSTRING_LEN, DAT_ERR_JFIELD_CLASS_NAME,
	DAT_ERR_JCLASS_TYPE, DAT_ERR_JCLASS_FIELDS, DAT_ERR_JCLASS_ANNOTATION,
	DAT_ERR_JOBJECT_TYPE, DAT_ERR_JARRAY_TYPE, DAT_ERR_JARRAY_CONTENT,
	/* CW map decoding errors */
	NBT_ERR_INT32S, NBT_ERR_UNKNOWN, CW_ERR_ROOT_TAG, CW_ERR_STRING_LEN
};
#endif
