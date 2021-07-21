// Copyright 2014 PDFium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Original code copyright 2014 Foxit Software Inc. http://www.foxitsoftware.com

// NOTE: External docs refer to this file as "fpdfview.h", so do not rename
// despite lack of consistency with other public files.

#ifndef PUBLIC_FPDFAPI_H_
#define PUBLIC_FPDFAPI_H_

#define CHEFTIN_EXT 1

#include <stddef.h>
#include <vector>
#include <string>
#include <fstream>
#include <map>

#if defined(_WIN32) && !defined(__WINDOWS__)
#include <windows.h>
#endif

#include "fpdfview.h"
#include "fpdf_save.h"
#include "fpdf_ppo.h"
#include "fpdf_annot.h"

// typedef std::vector<FPDF_PAGE_ITEM*> FPDF_PAGE_LIST;

// Exported Functions
#ifdef __cplusplus
extern "C" {
#endif

typedef std::vector<int> FPDF_INT_LIST;
typedef std::vector<wchar_t> FPDF_WCHAR_LIST;
typedef std::vector<char> FPDF_CHAR_LIST;
typedef std::wstring FPDF_STD_WSTRING;
typedef std::string FPDF_STD_STRING;

typedef struct _FPDF_PNG_ENCODING_ {
    FPDF_EXPORT _FPDF_PNG_ENCODING_();
    FPDF_EXPORT ~_FPDF_PNG_ENCODING_();
    FPDF_EXPORT _FPDF_PNG_ENCODING_(const _FPDF_PNG_ENCODING_& other);
    FPDF_EXPORT _FPDF_PNG_ENCODING_& operator=(const _FPDF_PNG_ENCODING_& other);
    std::vector<uint8_t> png_encoding;
} FPDF_PNG_ENCODING;

typedef struct _FPDF_RECT_ {
    FPDF_EXPORT _FPDF_RECT_();
    FPDF_EXPORT ~_FPDF_RECT_();
    FPDF_EXPORT _FPDF_RECT_(const _FPDF_RECT_& other);
    FPDF_EXPORT _FPDF_RECT_& operator=(const _FPDF_RECT_& other);
    float left;
    float right;
    float top;
    float bottom;
} FPDF_RECT;

typedef struct _FPDF_POINT_ {
    FPDF_EXPORT _FPDF_POINT_();
    FPDF_EXPORT ~_FPDF_POINT_();
    FPDF_EXPORT _FPDF_POINT_(const _FPDF_POINT_& other);
    FPDF_EXPORT _FPDF_POINT_& operator=(const _FPDF_POINT_& other);
    float x;
    float y;
    int type;
    bool close;
} FPDF_POINT;

typedef struct _FPDF_SIZE_ {
    FPDF_EXPORT _FPDF_SIZE_();
    FPDF_EXPORT ~_FPDF_SIZE_();
    FPDF_EXPORT _FPDF_SIZE_(const _FPDF_SIZE_& other);
    FPDF_EXPORT _FPDF_SIZE_& operator=(const _FPDF_SIZE_& other);
    float width;
    float height;
} FPDF_SIZE;

typedef struct _FPDF_COLOR_ {
    FPDF_EXPORT _FPDF_COLOR_();
    FPDF_EXPORT ~_FPDF_COLOR_();
    FPDF_EXPORT _FPDF_COLOR_(const _FPDF_COLOR_& other);
    FPDF_EXPORT _FPDF_COLOR_& operator=(const _FPDF_COLOR_& other);
    unsigned int r;
    unsigned int g;
    unsigned int b;
    unsigned int a;
} FPDF_COLOR;

typedef struct _FPDF_CHAR_ITEM_ {
    FPDF_EXPORT _FPDF_CHAR_ITEM_();
    FPDF_EXPORT ~_FPDF_CHAR_ITEM_();
    FPDF_EXPORT _FPDF_CHAR_ITEM_(const _FPDF_CHAR_ITEM_& other);
    FPDF_EXPORT _FPDF_CHAR_ITEM_& operator=(const _FPDF_CHAR_ITEM_& other);
    FPDF_RECT charBox;
    FPDF_RECT fontBox;
    std::string text;
    uint8_t flag;
} FPDF_CHAR_ITEM;

typedef struct _FPDF_TEXT_ITEM_ {
    FPDF_EXPORT _FPDF_TEXT_ITEM_();
    FPDF_EXPORT ~_FPDF_TEXT_ITEM_();
    FPDF_EXPORT _FPDF_TEXT_ITEM_(const _FPDF_TEXT_ITEM_& other);
    FPDF_EXPORT _FPDF_TEXT_ITEM_& operator=(const _FPDF_TEXT_ITEM_& other);
    FPDF_RECT bbox;
    FPDF_RECT clipBox;
    std::string text;
    std::string familyName;
    std::string faceName;
    float fontsize;
    int fontflags;
    int textMode;
    bool italic;
    bool bold;
    bool hasFont;
    FPDF_COLOR fillColor;
    FPDF_COLOR strokeColor;
    std::vector<FPDF_CHAR_ITEM> chars;
    int z_index;
} FPDF_TEXT_ITEM;

typedef struct _FPDF_PATH_ITEM_ {
    FPDF_EXPORT _FPDF_PATH_ITEM_();
    FPDF_EXPORT ~_FPDF_PATH_ITEM_();
    FPDF_EXPORT _FPDF_PATH_ITEM_(const _FPDF_PATH_ITEM_& other);
    FPDF_EXPORT _FPDF_PATH_ITEM_& operator=(const _FPDF_PATH_ITEM_& other);
    FPDF_RECT bbox;
    FPDF_RECT clipBox;
    bool isRect;
    bool isStroke;
    uint8_t fillType;
    float width;
    FPDF_COLOR fillColor;
    FPDF_COLOR strokeColor;
    std::vector<FPDF_POINT> points;
    int z_index;
} FPDF_PATH_ITEM;

typedef struct _FPDF_IMAGE_ITEM_ {
    FPDF_EXPORT _FPDF_IMAGE_ITEM_();
    FPDF_EXPORT ~_FPDF_IMAGE_ITEM_();
    FPDF_EXPORT _FPDF_IMAGE_ITEM_(const _FPDF_IMAGE_ITEM_& other);
    FPDF_EXPORT _FPDF_IMAGE_ITEM_& operator=(const _FPDF_IMAGE_ITEM_& other);
    FPDF_RECT bbox;
    FPDF_RECT clipBox;
    FPDF_COLOR fillColor;
    FPDF_COLOR strokeColor;
    std::vector<uint8_t> png_encoding;
    int z_index;
} FPDF_IMAGE_ITEM;

typedef struct _FPDF_PAGE_ITEM_ {
    FPDF_EXPORT _FPDF_PAGE_ITEM_();
    FPDF_EXPORT ~_FPDF_PAGE_ITEM_();
    FPDF_EXPORT _FPDF_PAGE_ITEM_(const _FPDF_PAGE_ITEM_& other);
    FPDF_EXPORT _FPDF_PAGE_ITEM_& operator=(const _FPDF_PAGE_ITEM_& other);
    int id;
    int rotation;
    int page_rotation;
    FPDF_SIZE size;
    std::vector<FPDF_TEXT_ITEM> texts;
    std::vector<FPDF_PATH_ITEM> paths;
    std::vector<FPDF_IMAGE_ITEM> images;
    std::map<std::string, std::string> glyphs;
} FPDF_PAGE_ITEM;

typedef struct _FPDF_BOOKMARK_ITEM_ {
    FPDF_EXPORT _FPDF_BOOKMARK_ITEM_();
    FPDF_EXPORT ~_FPDF_BOOKMARK_ITEM_();
    FPDF_EXPORT _FPDF_BOOKMARK_ITEM_(const _FPDF_BOOKMARK_ITEM_& other);
    FPDF_EXPORT _FPDF_BOOKMARK_ITEM_& operator=(const _FPDF_BOOKMARK_ITEM_& other);
    int index;
    int parent;
    int page;
    int level;
    FPDF_INT_LIST children;
    std::string title;
} FPDF_BOOKMARK_ITEM;

typedef struct _FPDF_BOOKMARKS_ITEM_ {
    FPDF_EXPORT _FPDF_BOOKMARKS_ITEM_();
    FPDF_EXPORT ~_FPDF_BOOKMARKS_ITEM_();
    FPDF_EXPORT _FPDF_BOOKMARKS_ITEM_(const _FPDF_BOOKMARKS_ITEM_& other);
    FPDF_EXPORT _FPDF_BOOKMARKS_ITEM_& operator=(const _FPDF_BOOKMARKS_ITEM_& other);
    int id;
    int rotation;
    FPDF_SIZE size;
    std::vector<FPDF_BOOKMARK_ITEM> bookmarks;
} FPDF_BOOKMARKS_ITEM;

class BufferFileWrite : public FPDF_FILEWRITE {
  public:
    BufferFileWrite(const std::string &file);
    ~BufferFileWrite();

  private:
    int DoWriteBlock(const void* data, unsigned long size);
    static int WriteBlockImpl(FPDF_FILEWRITE* this_file_write, const void* data, unsigned long size);

    std::ofstream _file;
};

typedef struct _FPDF_RECT_PFB_ {
    _FPDF_RECT_PFB_() : left(0), right(0), top(0), bottom(0) {}
    _FPDF_RECT_PFB_(const _FPDF_RECT_PFB_& other) {
        left = other.left;
        right = other.right;
        top = other.top;
        bottom = other.bottom;
    }
    _FPDF_RECT_PFB_(const _FPDF_RECT_& other) {
        left = other.left;
        right = other.right;
        top = other.top;
        bottom = other.bottom;
    }

    float left;
    float right;
    float top;
    float bottom;
} FPDF_RECT_PFB;

typedef struct _FPDF_IMAGE_ITEM_PFB_ {
    _FPDF_IMAGE_ITEM_PFB_() {}
    _FPDF_IMAGE_ITEM_PFB_(const _FPDF_IMAGE_ITEM_PFB_& other) {
        bbox = other.bbox;
        clipBox = other.clipBox;
    }

    FPDF_RECT_PFB bbox;
    FPDF_RECT_PFB clipBox;
} FPDF_IMAGE_ITEM_PFB;

typedef struct _FPDF_PAGE_ITEMS_PFB_ {
    _FPDF_PAGE_ITEMS_PFB_() : pImages(nullptr), image_counts(0) {}

    FPDF_IMAGE_ITEM_PFB* pImages;
    uint32_t image_counts;
} FPDF_PAGE_ITEMS_PFB;

typedef struct _FPDF_PAGE_INFO_ {
    _FPDF_PAGE_INFO_() : width(0), height(0), text_counts(0), path_counts(0), image_counts(0) {}

    int width;
    int height;
    uint32_t text_counts;
    uint32_t path_counts;
    uint32_t image_counts;
} FPDF_PAGE_INFO;

typedef struct _FPDF_TEXT_OBJ_INFO_ {
    double left;
    double bottom;
    float font_size;
    std::string text;
} FPDF_TEXT_OBJ_INFO;

typedef struct _FPDF_IMAGE_OBJ_INFO_ {
    double left;
    double bottom;
    double width;
    double height;
    std::string image_path;
} FPDF_IMAGE_OBJ_INFO;

typedef struct _FPDF_RECT_OBJ_INFO {
    float x;
    float y;
    float w;
    float h;
    unsigned int r;
    unsigned int g;
    unsigned int b;
    unsigned int a;
} FPDF_RECT_OBJ_INFO;

typedef struct _FPDF_ANNOT_ITEM_ {
    FPDF_EXPORT _FPDF_ANNOT_ITEM_();
    FPDF_EXPORT ~_FPDF_ANNOT_ITEM_();
    FPDF_EXPORT _FPDF_ANNOT_ITEM_(const _FPDF_ANNOT_ITEM_& other);
    FPDF_EXPORT _FPDF_ANNOT_ITEM_& operator=(const _FPDF_ANNOT_ITEM_& other);
    int subtype;
    FPDF_RECT rect;
    float fontsize;
    FPDF_COLOR color;
    std::vector<std::string> texts;
    std::vector<FPDF_RECT> attachment_rects;
} FPDF_ANNOT_ITEM;

FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
FPDF_GetPageItemForPFB(FPDF_PAGE page, FPDF_PAGE_ITEMS_PFB& item);

FPDF_EXPORT void FPDF_CALLCONV
FPDF_DestroyPageItemForPFB(FPDF_PAGE_ITEMS_PFB& item);

FPDF_EXPORT void FPDF_CALLCONV
FPDF_LoadPageObject(FPDF_PAGE page, FPDF_PAGE_ITEM& pageObj, bool saveGlyphs=false, bool saveImages=false);

FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
FPDF_SaveDocument(FPDF_DOCUMENT document, FPDF_STRING file_path);

FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
FPDF_SavePagesToFile(FPDF_DOCUMENT document, FPDF_BYTESTRING pagerange, FPDF_STRING file_path);

FPDF_EXPORT void FPDF_CALLCONV
FPDF_GetBookmarks(FPDF_DOCUMENT document, FPDF_BOOKMARKS_ITEM& bookmarksItem);

FPDF_EXPORT bool FPDF_CALLCONV
FPDF_SavePageBitmap(FPDF_PAGE page, FPDF_STRING path, double scaleX, double scaleY);

FPDF_EXPORT bool FPDF_CALLCONV
FPDF_GetPageBitmap(FPDF_PAGE page, FPDF_PNG_ENCODING& png_encoding, double scaleX, double scaleY);

FPDF_EXPORT bool FPDF_CALLCONV
FPDF_ExtractFont(FPDF_DOCUMENT document, FPDF_STRING font_name, FPDF_STRING save_path);

FPDF_EXPORT uint8_t* FPDF_CALLCONV
FPDF_ExtractPageContents(FPDF_PAGE page, uint32_t& length);

FPDF_EXPORT uint8_t* FPDF_CALLCONV
FPDF_ExtractPageImageResources(FPDF_PAGE page, uint32_t& length);

FPDF_EXPORT void FPDF_CALLCONV
FPDF_GetSystemFonts(std::map<std::string, std::string>&);

FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
FPDF_CreatePDFDocumentFromImages(std::vector<std::string>& images_path, std::vector<int>& pages_width, std::vector<int>& pages_height, const char* file_path);

FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
FPDF_GetFPDFPageMatrix(FPDF_PAGE page, double* a, double* b, double* c, double* d, double* e, double* f);

FPDF_EXPORT void FPDF_CALLCONV
FPDF_GetPageInfo(FPDF_PAGE page, FPDF_PAGE_INFO& info);

FPDF_EXPORT void FPDF_CALLCONV
FPDFPage_InsertTextObject(FPDF_DOCUMENT document, FPDF_PAGE page, FPDF_TEXT_OBJ_INFO& text_info, FPDF_FONT font);

FPDF_EXPORT void FPDF_CALLCONV
FPDFPage_InsertImageObject(FPDF_DOCUMENT document, FPDF_PAGE page, FPDF_IMAGE_OBJ_INFO& image_info);

FPDF_EXPORT void FPDF_CALLCONV
FPDFPage_InsertRectObject(FPDF_PAGE page, FPDF_RECT_OBJ_INFO& rect_info);

FPDF_EXPORT bool FPDF_CALLCONV
FPDF_CreateAnnot(FPDF_DOCUMENT document, FPDF_PAGE page, FPDF_FONT font, FPDF_ANNOT_ITEM& info);

FPDF_EXPORT bool FPDF_CALLCONV
FPDF_GetPageAnnots(FPDF_PAGE page, std::vector<FPDF_ANNOT_ITEM>& items);

FPDF_EXPORT void FPDF_CALLCONV
FPDFRect_DeviceToPage(FPDF_PAGE page, FPDF_RECT& rect);
#ifdef __cplusplus
}
#endif


FPDF_EXPORT std::string FPDF_WStringToString(const std::wstring& src);
FPDF_EXPORT std::string FPDF_WCharToString(const wchar_t wc);
FPDF_EXPORT std::string FPDF_SurrogatePairToString(wchar_t h, wchar_t l);

FPDF_EXPORT void FPDF_SetPageOpaqueData(FPDF_DOCUMENT document, int index, std::string key, std::string value);
FPDF_EXPORT std::string FPDF_GetPageOpaqueData(FPDF_DOCUMENT document, int index, std::string key);
FPDF_EXPORT void FPDF_SetGlobalOpaqueData(FPDF_DOCUMENT document, std::string key, std::string value);
FPDF_EXPORT std::string FPDF_GetGlobalOpaqueData(FPDF_DOCUMENT document, std::string key);

FPDF_EXPORT std::string FPDF_GetPageContentStream(FPDF_DOCUMENT document, int index);
FPDF_EXPORT void FPDF_SetPageContentStream(FPDF_DOCUMENT document, int index, std::string value);

FPDF_EXPORT int FPDF_GetPageRotation(FPDF_PAGE page);

FPDF_EXPORT FPDF_FONT FPDF_ImportFont(FPDF_DOCUMENT document, std::string file_path, int type, bool cid);
#endif  // PUBLIC_FPDFAPI_H_
