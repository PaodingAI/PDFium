// Copyright 2016 PDFium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Original code copyright 2014 Foxit Software Inc. http://www.foxitsoftware.com

#include "core/fpdfapi/parser/cpdf_parser.h"

#include <algorithm>
#include <utility>
#include <vector>

#include "core/fpdfapi/parser/cpdf_array.h"
#include "core/fpdfapi/parser/cpdf_crypto_handler.h"
#include "core/fpdfapi/parser/cpdf_dictionary.h"
#include "core/fpdfapi/parser/cpdf_document.h"
#include "core/fpdfapi/parser/cpdf_linearized_header.h"
#include "core/fpdfapi/parser/cpdf_number.h"
#include "core/fpdfapi/parser/cpdf_reference.h"
#include "core/fpdfapi/parser/cpdf_security_handler.h"
#include "core/fpdfapi/parser/cpdf_stream.h"
#include "core/fpdfapi/parser/cpdf_stream_acc.h"
#include "core/fpdfapi/parser/cpdf_syntax_parser.h"
#include "core/fpdfapi/parser/fpdf_parser_utility.h"
#include "core/fxcrt/cfx_memorystream.h"
#include "core/fxcrt/fx_extension.h"
#include "core/fxcrt/fx_safe_types.h"
#include "third_party/base/ptr_util.h"
#include "third_party/base/stl_util.h"

namespace {

// A limit on the size of the xref table. Theoretical limits are higher, but
// this may be large enough in practice.
const int32_t kMaxXRefSize = 1048576;

uint32_t GetVarInt(const uint8_t* p, int32_t n) {
  uint32_t result = 0;
  for (int32_t i = 0; i < n; ++i)
    result = result * 256 + p[i];
  return result;
}

int32_t GetStreamNCount(const CFX_RetainPtr<CPDF_StreamAcc>& pObjStream) {
  return pObjStream->GetDict()->GetIntegerFor("N");
}

int32_t GetStreamFirst(const CFX_RetainPtr<CPDF_StreamAcc>& pObjStream) {
  return pObjStream->GetDict()->GetIntegerFor("First");
}

CPDF_Parser::ObjectType GetObjectTypeFromCrossRefStreamType(
    int cross_ref_stream_type) {
  switch (cross_ref_stream_type) {
    case 0:
      return CPDF_Parser::ObjectType::kFree;
    case 1:
      return CPDF_Parser::ObjectType::kNotCompressed;
    case 2:
      return CPDF_Parser::ObjectType::kCompressed;
    default:
      return CPDF_Parser::ObjectType::kNull;
  }
}

}  // namespace

CPDF_Parser::CPDF_Parser()
    : m_pSyntax(pdfium::MakeUnique<CPDF_SyntaxParser>()),
      m_bHasParsed(false),
      m_bXRefStream(false),
      m_bVersionUpdated(false),
      m_FileVersion(0),
      m_pEncryptDict(nullptr),
      m_TrailerPos(CPDF_Parser::kInvalidPos),
      m_dwXrefStartObjNum(0) {}

CPDF_Parser::~CPDF_Parser() {
  ReleaseEncryptHandler();
}

uint32_t CPDF_Parser::GetLastObjNum() const {
  return m_ObjectInfo.empty() ? 0 : m_ObjectInfo.rbegin()->first;
}

bool CPDF_Parser::IsValidObjectNumber(uint32_t objnum) const {
  return !m_ObjectInfo.empty() && objnum <= m_ObjectInfo.rbegin()->first;
}

FX_FILESIZE CPDF_Parser::GetObjectPositionOrZero(uint32_t objnum) const {
  auto it = m_ObjectInfo.find(objnum);
  return it != m_ObjectInfo.end() ? it->second.pos : 0;
}

CPDF_Parser::ObjectType CPDF_Parser::GetObjectType(uint32_t objnum) const {
  ASSERT(IsValidObjectNumber(objnum));
  auto it = m_ObjectInfo.find(objnum);
  return it != m_ObjectInfo.end() ? it->second.type : ObjectType::kFree;
}

uint16_t CPDF_Parser::GetObjectGenNum(uint32_t objnum) const {
  ASSERT(IsValidObjectNumber(objnum));
  auto it = m_ObjectInfo.find(objnum);
  return it != m_ObjectInfo.end() ? it->second.gennum : 0;
}

bool CPDF_Parser::IsObjectFreeOrNull(uint32_t objnum) const {
  switch (GetObjectType(objnum)) {
    case ObjectType::kFree:
    case ObjectType::kNull:
      return true;
    case ObjectType::kNotCompressed:
    case ObjectType::kCompressed:
      return false;
  }
  ASSERT(false);  // NOTREACHED();
  return false;
}

void CPDF_Parser::SetEncryptDictionary(CPDF_Dictionary* pDict) {
  m_pEncryptDict = pDict;
}

CFX_RetainPtr<CPDF_CryptoHandler> CPDF_Parser::GetCryptoHandler() const {
  return m_pSyntax->m_pCryptoHandler;
}

CFX_RetainPtr<IFX_SeekableReadStream> CPDF_Parser::GetFileAccess() const {
  return m_pSyntax->m_pFileAccess;
}

void CPDF_Parser::ShrinkObjectMap(uint32_t objnum) {
  if (objnum == 0) {
    m_ObjectInfo.clear();
    return;
  }

  auto it = m_ObjectInfo.lower_bound(objnum);
  while (it != m_ObjectInfo.end()) {
    auto saved_it = it++;
    m_ObjectInfo.erase(saved_it);
  }

  if (!pdfium::ContainsKey(m_ObjectInfo, objnum - 1))
    m_ObjectInfo[objnum - 1].pos = 0;
}

CPDF_Parser::Error CPDF_Parser::StartParse(
    const CFX_RetainPtr<IFX_SeekableReadStream>& pFileAccess,
    CPDF_Document* pDocument) {
  ASSERT(!m_bHasParsed);
  m_bHasParsed = true;
  m_bXRefStream = false;
  m_LastXRefOffset = 0;

  int32_t offset = GetHeaderOffset(pFileAccess);
  if (offset == -1)
    return FORMAT_ERROR;

  m_pSyntax->InitParser(pFileAccess, offset);

  uint8_t ch;
  if (!m_pSyntax->GetCharAt(5, ch))
    return FORMAT_ERROR;

  if (std::isdigit(ch))
    m_FileVersion = FXSYS_DecimalCharToInt(static_cast<wchar_t>(ch)) * 10;

  if (!m_pSyntax->GetCharAt(7, ch))
    return FORMAT_ERROR;

  if (std::isdigit(ch))
    m_FileVersion += FXSYS_DecimalCharToInt(static_cast<wchar_t>(ch));

  if (m_pSyntax->m_FileLen < m_pSyntax->m_HeaderOffset + 9)
    return FORMAT_ERROR;

  m_pSyntax->SetPos(m_pSyntax->m_FileLen - m_pSyntax->m_HeaderOffset - 9);
  m_pDocument = pDocument;

  bool bXRefRebuilt = false;
  if (m_pSyntax->BackwardsSearchToWord("startxref", 4096)) {
    m_SortedOffset.insert(m_pSyntax->GetPos());
    m_pSyntax->GetKeyword();

    bool bNumber;
    CFX_ByteString xrefpos_str = m_pSyntax->GetNextWord(&bNumber);
    if (!bNumber)
      return FORMAT_ERROR;

    m_LastXRefOffset = (FX_FILESIZE)FXSYS_atoi64(xrefpos_str.c_str());
    if (!LoadAllCrossRefV4(m_LastXRefOffset) &&
        !LoadAllCrossRefV5(m_LastXRefOffset)) {
      if (!RebuildCrossRef())
        return FORMAT_ERROR;

      bXRefRebuilt = true;
      m_LastXRefOffset = 0;
    }
  } else {
    if (!RebuildCrossRef())
      return FORMAT_ERROR;

    bXRefRebuilt = true;
  }
  Error eRet = SetEncryptHandler();
  if (eRet != SUCCESS)
    return eRet;

  m_pDocument->LoadDoc();
  if (!m_pDocument->GetRoot() || m_pDocument->GetPageCount() == 0) {
    if (bXRefRebuilt)
      return FORMAT_ERROR;

    ReleaseEncryptHandler();
    if (!RebuildCrossRef())
      return FORMAT_ERROR;

    eRet = SetEncryptHandler();
    if (eRet != SUCCESS)
      return eRet;

    m_pDocument->LoadDoc();
    if (!m_pDocument->GetRoot())
      return FORMAT_ERROR;
  }
  if (GetRootObjNum() == 0) {
    ReleaseEncryptHandler();
    if (!RebuildCrossRef() || GetRootObjNum() == 0)
      return FORMAT_ERROR;

    eRet = SetEncryptHandler();
    if (eRet != SUCCESS)
      return eRet;
  }
  if (m_pSecurityHandler && !m_pSecurityHandler->IsMetadataEncrypted()) {
    CPDF_Reference* pMetadata =
        ToReference(m_pDocument->GetRoot()->GetObjectFor("Metadata"));
    if (pMetadata)
      m_pSyntax->m_MetadataObjnum = pMetadata->GetRefObjNum();
  }
  return SUCCESS;
}

CPDF_Parser::Error CPDF_Parser::SetEncryptHandler() {
  ReleaseEncryptHandler();
  if (!GetTrailer())
    return FORMAT_ERROR;

  CPDF_Object* pEncryptObj = GetTrailer()->GetObjectFor("Encrypt");
  if (pEncryptObj) {
    if (CPDF_Dictionary* pEncryptDict = pEncryptObj->AsDictionary()) {
      SetEncryptDictionary(pEncryptDict);
    } else if (CPDF_Reference* pRef = pEncryptObj->AsReference()) {
      pEncryptObj = m_pDocument->GetOrParseIndirectObject(pRef->GetRefObjNum());
      if (pEncryptObj)
        SetEncryptDictionary(pEncryptObj->GetDict());
    }
  }

  if (m_pEncryptDict) {
    CFX_ByteString filter = m_pEncryptDict->GetStringFor("Filter");
    if (filter != "Standard")
      return HANDLER_ERROR;

    std::unique_ptr<CPDF_SecurityHandler> pSecurityHandler =
        pdfium::MakeUnique<CPDF_SecurityHandler>();
    if (!pSecurityHandler->OnInit(this, m_pEncryptDict.Get()))
      return PASSWORD_ERROR;

    m_pSecurityHandler = std::move(pSecurityHandler);
    auto pCryptoHandler = pdfium::MakeRetain<CPDF_CryptoHandler>();
    if (!pCryptoHandler->Init(m_pEncryptDict.Get(), m_pSecurityHandler.get()))
      return HANDLER_ERROR;
    m_pSyntax->SetEncrypt(pCryptoHandler);
  }
  return SUCCESS;
}

void CPDF_Parser::ReleaseEncryptHandler() {
  m_pSyntax->m_pCryptoHandler.Reset();
  m_pSecurityHandler.reset();
  SetEncryptDictionary(nullptr);
}

FX_FILESIZE CPDF_Parser::GetObjectOffset(uint32_t objnum) const {
  if (!IsValidObjectNumber(objnum))
    return 0;

  if (GetObjectType(objnum) == ObjectType::kNotCompressed)
    return GetObjectPositionOrZero(objnum);

  if (GetObjectType(objnum) == ObjectType::kCompressed) {
    FX_FILESIZE pos = GetObjectPositionOrZero(objnum);
    return GetObjectPositionOrZero(pos);
  }
  return 0;
}

// Ideally, all the cross reference entries should be verified.
// In reality, we rarely see well-formed cross references don't match
// with the objects. crbug/602650 showed a case where object numbers
// in the cross reference table are all off by one.
bool CPDF_Parser::VerifyCrossRefV4() {
  for (const auto& it : m_ObjectInfo) {
    if (it.second.pos == 0)
      continue;
    // Find the first non-zero position.
    FX_FILESIZE SavedPos = m_pSyntax->GetPos();
    m_pSyntax->SetPos(it.second.pos);
    bool is_num = false;
    CFX_ByteString num_str = m_pSyntax->GetNextWord(&is_num);
    m_pSyntax->SetPos(SavedPos);
    if (!is_num || num_str.IsEmpty() ||
        FXSYS_atoui(num_str.c_str()) != it.first) {
      // If the object number read doesn't match the one stored,
      // something is wrong with the cross reference table.
      return false;
    } else {
      return true;
    }
  }
  return true;
}

bool CPDF_Parser::LoadAllCrossRefV4(FX_FILESIZE xrefpos) {
  if (!LoadCrossRefV4(xrefpos, 0, true))
    return false;

  std::unique_ptr<CPDF_Dictionary> trailer = LoadTrailerV4();
  if (!trailer)
    return false;

  m_Trailers.push_back(std::move(trailer));
  m_TrailerPos = m_Trailers.size() - 1;
  int32_t xrefsize = GetDirectInteger(GetTrailer(), "Size");
  if (xrefsize > 0 && xrefsize <= kMaxXRefSize)
    ShrinkObjectMap(xrefsize);

  std::vector<FX_FILESIZE> CrossRefList;
  std::vector<FX_FILESIZE> XRefStreamList;
  std::set<FX_FILESIZE> seen_xrefpos;

  CrossRefList.push_back(xrefpos);
  XRefStreamList.push_back(GetDirectInteger(GetTrailer(), "XRefStm"));
  seen_xrefpos.insert(xrefpos);

  // When the trailer doesn't have Prev entry or Prev entry value is not
  // numerical, GetDirectInteger() returns 0. Loading will end.
  xrefpos = GetDirectInteger(GetTrailer(), "Prev");
  while (xrefpos) {
    // Check for circular references.
    if (pdfium::ContainsKey(seen_xrefpos, xrefpos))
      return false;

    seen_xrefpos.insert(xrefpos);

    // SLOW ...
    CrossRefList.insert(CrossRefList.begin(), xrefpos);
    LoadCrossRefV4(xrefpos, 0, true);

    std::unique_ptr<CPDF_Dictionary> pDict(LoadTrailerV4());
    if (!pDict)
      return false;

    xrefpos = GetDirectInteger(pDict.get(), "Prev");

    // SLOW ...
    XRefStreamList.insert(XRefStreamList.begin(),
                          pDict->GetIntegerFor("XRefStm"));
    m_Trailers.push_back(std::move(pDict));
  }

  for (size_t i = 0; i < CrossRefList.size(); ++i) {
    if (!LoadCrossRefV4(CrossRefList[i], XRefStreamList[i], false))
      return false;
    if (i == 0 && !VerifyCrossRefV4())
      return false;
  }
  return true;
}

bool CPDF_Parser::LoadLinearizedAllCrossRefV4(FX_FILESIZE xrefpos,
                                              uint32_t dwObjCount) {
  if (!LoadLinearizedCrossRefV4(xrefpos, dwObjCount))
    return false;

  std::unique_ptr<CPDF_Dictionary> trailer = LoadTrailerV4();
  if (!trailer)
    return false;

  m_Trailers.push_back(std::move(trailer));
  m_TrailerPos = m_Trailers.size() - 1;
  int32_t xrefsize = GetDirectInteger(GetTrailer(), "Size");
  if (xrefsize == 0)
    return false;

  std::vector<FX_FILESIZE> CrossRefList;
  std::vector<FX_FILESIZE> XRefStreamList;
  std::set<FX_FILESIZE> seen_xrefpos;

  CrossRefList.push_back(xrefpos);
  XRefStreamList.push_back(GetDirectInteger(GetTrailer(), "XRefStm"));
  seen_xrefpos.insert(xrefpos);

  xrefpos = GetDirectInteger(GetTrailer(), "Prev");
  while (xrefpos) {
    // Check for circular references.
    if (pdfium::ContainsKey(seen_xrefpos, xrefpos))
      return false;

    seen_xrefpos.insert(xrefpos);

    // SLOW ...
    CrossRefList.insert(CrossRefList.begin(), xrefpos);
    LoadCrossRefV4(xrefpos, 0, true);

    std::unique_ptr<CPDF_Dictionary> pDict(LoadTrailerV4());
    if (!pDict)
      return false;

    xrefpos = GetDirectInteger(pDict.get(), "Prev");

    // SLOW ...
    XRefStreamList.insert(XRefStreamList.begin(),
                          pDict->GetIntegerFor("XRefStm"));
    m_Trailers.push_back(std::move(pDict));
  }

  for (size_t i = 1; i < CrossRefList.size(); ++i) {
    if (!LoadCrossRefV4(CrossRefList[i], XRefStreamList[i], false))
      return false;
  }
  return true;
}

bool CPDF_Parser::LoadLinearizedCrossRefV4(FX_FILESIZE pos,
                                           uint32_t dwObjCount) {
  FX_FILESIZE dwStartPos = pos - m_pSyntax->m_HeaderOffset;

  m_pSyntax->SetPos(dwStartPos);
  m_SortedOffset.insert(pos);

  uint32_t start_objnum = 0;
  uint32_t count = dwObjCount;
  FX_FILESIZE SavedPos = m_pSyntax->GetPos();

  const int32_t recordsize = 20;
  std::vector<char> buf(1024 * recordsize + 1);
  buf[1024 * recordsize] = '\0';

  int32_t nBlocks = count / 1024 + 1;
  for (int32_t block = 0; block < nBlocks; block++) {
    int32_t block_size = block == nBlocks - 1 ? count % 1024 : 1024;
    uint32_t dwReadSize = block_size * recordsize;
    if ((FX_FILESIZE)(dwStartPos + dwReadSize) > m_pSyntax->m_FileLen)
      return false;

    if (!m_pSyntax->ReadBlock(reinterpret_cast<uint8_t*>(buf.data()),
                              dwReadSize)) {
      return false;
    }

    for (int32_t i = 0; i < block_size; i++) {
      uint32_t objnum = start_objnum + block * 1024 + i;
      char* pEntry = &buf[i * recordsize];
      if (pEntry[17] == 'f') {
        m_ObjectInfo[objnum].pos = 0;
        m_ObjectInfo[objnum].type = ObjectType::kFree;
      } else {
        int32_t offset = FXSYS_atoi(pEntry);
        if (offset == 0) {
          for (int32_t c = 0; c < 10; c++) {
            if (!std::isdigit(pEntry[c]))
              return false;
          }
        }

        m_ObjectInfo[objnum].pos = offset;
        int32_t version = FXSYS_atoi(pEntry + 11);
        if (version >= 1)
          m_bVersionUpdated = true;

        m_ObjectInfo[objnum].gennum = version;
        if (m_ObjectInfo[objnum].pos < m_pSyntax->m_FileLen)
          m_SortedOffset.insert(m_ObjectInfo[objnum].pos);

        m_ObjectInfo[objnum].type = ObjectType::kNotCompressed;
      }
    }
  }
  m_pSyntax->SetPos(SavedPos + count * recordsize);
  return true;
}

bool CPDF_Parser::LoadCrossRefV4(FX_FILESIZE pos,
                                 FX_FILESIZE streampos,
                                 bool bSkip) {
  m_pSyntax->SetPos(pos);
  if (m_pSyntax->GetKeyword() != "xref")
    return false;

  m_SortedOffset.insert(pos);
  if (streampos)
    m_SortedOffset.insert(streampos);

  while (1) {
    FX_FILESIZE SavedPos = m_pSyntax->GetPos();
    bool bIsNumber;
    CFX_ByteString word = m_pSyntax->GetNextWord(&bIsNumber);
    if (word.IsEmpty())
      return false;

    if (!bIsNumber) {
      m_pSyntax->SetPos(SavedPos);
      break;
    }

    uint32_t start_objnum = FXSYS_atoui(word.c_str());
    if (start_objnum >= kMaxObjectNumber)
      return false;

    uint32_t count = m_pSyntax->GetDirectNum();
    m_pSyntax->ToNextWord();
    SavedPos = m_pSyntax->GetPos();
    const int32_t recordsize = 20;

    m_dwXrefStartObjNum = start_objnum;
    if (!bSkip) {
      std::vector<char> buf(1024 * recordsize + 1);
      buf[1024 * recordsize] = '\0';

      int32_t nBlocks = count / 1024 + 1;
      for (int32_t block = 0; block < nBlocks; block++) {
        int32_t block_size = block == nBlocks - 1 ? count % 1024 : 1024;
        m_pSyntax->ReadBlock(reinterpret_cast<uint8_t*>(buf.data()),
                             block_size * recordsize);

        for (int32_t i = 0; i < block_size; i++) {
          uint32_t objnum = start_objnum + block * 1024 + i;
          char* pEntry = &buf[i * recordsize];
          if (pEntry[17] == 'f') {
            m_ObjectInfo[objnum].pos = 0;
            m_ObjectInfo[objnum].type = ObjectType::kFree;
          } else {
            FX_FILESIZE offset = (FX_FILESIZE)FXSYS_atoi64(pEntry);
            if (offset == 0) {
              for (int32_t c = 0; c < 10; c++) {
                if (!std::isdigit(pEntry[c]))
                  return false;
              }
            }

            m_ObjectInfo[objnum].pos = offset;
            int32_t version = FXSYS_atoi(pEntry + 11);
            if (version >= 1)
              m_bVersionUpdated = true;

            m_ObjectInfo[objnum].gennum = version;
            if (m_ObjectInfo[objnum].pos < m_pSyntax->m_FileLen)
              m_SortedOffset.insert(m_ObjectInfo[objnum].pos);

            m_ObjectInfo[objnum].type = ObjectType::kNotCompressed;
          }
        }
      }
    }
    m_pSyntax->SetPos(SavedPos + count * recordsize);
  }
  return !streampos || LoadCrossRefV5(&streampos, false);
}

bool CPDF_Parser::LoadAllCrossRefV5(FX_FILESIZE xrefpos) {
  if (!LoadCrossRefV5(&xrefpos, true))
    return false;

  std::set<FX_FILESIZE> seen_xrefpos;
  while (xrefpos) {
    seen_xrefpos.insert(xrefpos);
    if (!LoadCrossRefV5(&xrefpos, false))
      return false;

    // Check for circular references.
    if (pdfium::ContainsKey(seen_xrefpos, xrefpos))
      return false;
  }
  m_ObjectStreamMap.clear();
  m_bXRefStream = true;
  return true;
}

bool CPDF_Parser::RebuildCrossRef() {
  m_ObjectInfo.clear();
  m_SortedOffset.clear();
  m_Trailers.clear();
  m_TrailerPos = CPDF_Parser::kInvalidPos;

  ParserState state = ParserState::kDefault;
  int32_t inside_index = 0;
  uint32_t objnum = 0;
  uint32_t gennum = 0;
  int32_t depth = 0;
  const uint32_t kBufferSize = 4096;
  std::vector<uint8_t> buffer(kBufferSize);

  FX_FILESIZE pos = m_pSyntax->m_HeaderOffset;
  FX_FILESIZE start_pos = 0;
  FX_FILESIZE start_pos1 = 0;
  FX_FILESIZE last_obj = -1;
  FX_FILESIZE last_xref = -1;
  FX_FILESIZE last_trailer = -1;

  while (pos < m_pSyntax->m_FileLen) {
    const FX_FILESIZE saved_pos = pos;
    bool bOverFlow = false;
    uint32_t size =
        std::min((uint32_t)(m_pSyntax->m_FileLen - pos), kBufferSize);
    if (!m_pSyntax->m_pFileAccess->ReadBlock(buffer.data(), pos, size))
      break;

    for (uint32_t i = 0; i < size; i++) {
      uint8_t byte = buffer[i];
      switch (state) {
        case ParserState::kDefault:
          if (PDFCharIsWhitespace(byte)) {
            state = ParserState::kWhitespace;
          } else if (std::isdigit(byte)) {
            --i;
            state = ParserState::kWhitespace;
          } else if (byte == '%') {
            inside_index = 0;
            state = ParserState::kComment;
          } else if (byte == '(') {
            state = ParserState::kString;
            depth = 1;
          } else if (byte == '<') {
            inside_index = 1;
            state = ParserState::kHexString;
          } else if (byte == '\\') {
            state = ParserState::kEscapedString;
          } else if (byte == 't') {
            state = ParserState::kTrailer;
            inside_index = 1;
          }
          break;

        case ParserState::kWhitespace:
          if (std::isdigit(byte)) {
            start_pos = pos + i;
            state = ParserState::kObjNum;
            objnum = FXSYS_DecimalCharToInt(static_cast<wchar_t>(byte));
          } else if (byte == 't') {
            state = ParserState::kTrailer;
            inside_index = 1;
          } else if (byte == 'x') {
            state = ParserState::kXref;
            inside_index = 1;
          } else if (!PDFCharIsWhitespace(byte)) {
            --i;
            state = ParserState::kDefault;
          }
          break;

        case ParserState::kObjNum:
          if (std::isdigit(byte)) {
            objnum = objnum * 10 +
                     FXSYS_DecimalCharToInt(static_cast<wchar_t>(byte));
          } else if (PDFCharIsWhitespace(byte)) {
            state = ParserState::kPostObjNum;
          } else {
            --i;
            state = ParserState::kEndObj;
            inside_index = 0;
          }
          break;

        case ParserState::kPostObjNum:
          if (std::isdigit(byte)) {
            start_pos1 = pos + i;
            state = ParserState::kGenNum;
            gennum = FXSYS_DecimalCharToInt(static_cast<wchar_t>(byte));
          } else if (byte == 't') {
            state = ParserState::kTrailer;
            inside_index = 1;
          } else if (!PDFCharIsWhitespace(byte)) {
            --i;
            state = ParserState::kDefault;
          }
          break;

        case ParserState::kGenNum:
          if (std::isdigit(byte)) {
            gennum = gennum * 10 +
                     FXSYS_DecimalCharToInt(static_cast<wchar_t>(byte));
          } else if (PDFCharIsWhitespace(byte)) {
            state = ParserState::kPostGenNum;
          } else {
            --i;
            state = ParserState::kDefault;
          }
          break;

        case ParserState::kPostGenNum:
          if (byte == 'o') {
            state = ParserState::kBeginObj;
            inside_index = 1;
          } else if (std::isdigit(byte)) {
            objnum = gennum;
            gennum = FXSYS_DecimalCharToInt(static_cast<wchar_t>(byte));
            start_pos = start_pos1;
            start_pos1 = pos + i;
            state = ParserState::kGenNum;
          } else if (byte == 't') {
            state = ParserState::kTrailer;
            inside_index = 1;
          } else if (!PDFCharIsWhitespace(byte)) {
            --i;
            state = ParserState::kDefault;
          }
          break;

        case ParserState::kBeginObj:
          switch (inside_index) {
            case 1:
              if (byte != 'b') {
                --i;
                state = ParserState::kDefault;
              } else {
                inside_index++;
              }
              break;
            case 2:
              if (byte != 'j') {
                --i;
                state = ParserState::kDefault;
              } else {
                inside_index++;
              }
              break;
            case 3:
              if (PDFCharIsWhitespace(byte) || PDFCharIsDelimiter(byte)) {
                FX_FILESIZE obj_pos = start_pos - m_pSyntax->m_HeaderOffset;
                m_SortedOffset.insert(obj_pos);
                last_obj = start_pos;
                FX_FILESIZE obj_end = 0;
                std::unique_ptr<CPDF_Object> pObject =
                    ParseIndirectObjectAtByStrict(m_pDocument.Get(), obj_pos,
                                                  objnum, &obj_end);
                if (CPDF_Stream* pStream = ToStream(pObject.get())) {
                  if (CPDF_Dictionary* pDict = pStream->GetDict()) {
                    if ((pDict->KeyExist("Type")) &&
                        (pDict->GetStringFor("Type") == "XRef" &&
                         pDict->KeyExist("Size"))) {
                      CPDF_Object* pRoot = pDict->GetObjectFor("Root");
                      if (pRoot && pRoot->GetDict() &&
                          pRoot->GetDict()->GetObjectFor("Pages")) {
                        m_Trailers.push_back(ToDictionary(pDict->Clone()));
                        m_TrailerPos = m_Trailers.size() - 1;
                      }
                    }
                  }
                }

                FX_FILESIZE offset = 0;
                m_pSyntax->SetPos(obj_pos);
                offset = m_pSyntax->FindTag("obj", 0);
                if (offset == -1)
                  offset = 0;
                else
                  offset += 3;

                FX_FILESIZE nLen = obj_end - obj_pos - offset;
                if ((uint32_t)nLen > size - i) {
                  pos = obj_end + m_pSyntax->m_HeaderOffset;
                  bOverFlow = true;
                } else {
                  i += (uint32_t)nLen;
                }

                if (!m_ObjectInfo.empty() && IsValidObjectNumber(objnum) &&
                    m_ObjectInfo[objnum].pos) {
                  if (pObject) {
                    uint32_t oldgen = GetObjectGenNum(objnum);
                    m_ObjectInfo[objnum].pos = obj_pos;
                    m_ObjectInfo[objnum].gennum = gennum;
                    if (oldgen != gennum)
                      m_bVersionUpdated = true;
                  }
                } else {
                  m_ObjectInfo[objnum].pos = obj_pos;
                  m_ObjectInfo[objnum].type = ObjectType::kNotCompressed;
                  m_ObjectInfo[objnum].gennum = gennum;
                }
              }
              --i;
              state = ParserState::kDefault;
              break;
          }
          break;

        case ParserState::kTrailer:
          if (inside_index == 7) {
            if (PDFCharIsWhitespace(byte) || PDFCharIsDelimiter(byte)) {
              last_trailer = pos + i - 7;
              m_pSyntax->SetPos(pos + i - m_pSyntax->m_HeaderOffset);

              std::unique_ptr<CPDF_Object> pObj =
                  m_pSyntax->GetObject(m_pDocument.Get(), 0, 0, true);
              if (pObj) {
                if (pObj->IsDictionary() || pObj->AsStream()) {
                  CPDF_Stream* pStream = pObj->AsStream();
                  if (CPDF_Dictionary* pTrailer =
                          pStream ? pStream->GetDict() : pObj->AsDictionary()) {
                    if (GetTrailer()) {
                      CPDF_Object* pRoot = pTrailer->GetObjectFor("Root");
                      CPDF_Reference* pRef = ToReference(pRoot);
                      if (!pRoot ||
                          (pRef && IsValidObjectNumber(pRef->GetRefObjNum()) &&
                           m_ObjectInfo[pRef->GetRefObjNum()].pos != 0)) {
                        auto it = pTrailer->begin();
                        while (it != pTrailer->end()) {
                          const CFX_ByteString& key = it->first;
                          CPDF_Object* pElement = it->second.get();
                          ++it;
                          uint32_t dwObjNum =
                              pElement ? pElement->GetObjNum() : 0;
                          if (dwObjNum) {
                            GetTrailer()->SetNewFor<CPDF_Reference>(
                                key, m_pDocument.Get(), dwObjNum);
                          } else {
                            GetTrailer()->SetFor(key, pElement->Clone());
                          }
                        }
                      }
                    } else {
                      if (pObj->IsStream())
                        m_Trailers.push_back(ToDictionary(pTrailer->Clone()));
                      else
                        m_Trailers.push_back(ToDictionary(std::move(pObj)));
                      m_TrailerPos = m_Trailers.size() - 1;

                      FX_FILESIZE dwSavePos = m_pSyntax->GetPos();
                      CFX_ByteString strWord = m_pSyntax->GetKeyword();
                      if (!strWord.Compare("startxref")) {
                        bool bNumber;
                        CFX_ByteString bsOffset =
                            m_pSyntax->GetNextWord(&bNumber);
                        if (bNumber)
                          m_LastXRefOffset = FXSYS_atoi(bsOffset.c_str());
                      }
                      m_pSyntax->SetPos(dwSavePos);
                    }
                  }
                }
              }
            }
            --i;
            state = ParserState::kDefault;
          } else if (byte == "trailer"[inside_index]) {
            inside_index++;
          } else {
            --i;
            state = ParserState::kDefault;
          }
          break;

        case ParserState::kXref:
          if (inside_index == 4) {
            last_xref = pos + i - 4;
            state = ParserState::kWhitespace;
          } else if (byte == "xref"[inside_index]) {
            inside_index++;
          } else {
            --i;
            state = ParserState::kDefault;
          }
          break;

        case ParserState::kComment:
          if (PDFCharIsLineEnding(byte))
            state = ParserState::kDefault;
          break;

        case ParserState::kString:
          if (byte == ')') {
            if (depth > 0)
              depth--;
          } else if (byte == '(') {
            depth++;
          }

          if (!depth)
            state = ParserState::kDefault;
          break;

        case ParserState::kHexString:
          if (byte == '>' || (byte == '<' && inside_index == 1))
            state = ParserState::kDefault;
          inside_index = 0;
          break;

        case ParserState::kEscapedString:
          if (PDFCharIsDelimiter(byte) || PDFCharIsWhitespace(byte)) {
            --i;
            state = ParserState::kDefault;
          }
          break;

        case ParserState::kEndObj:
          if (PDFCharIsWhitespace(byte)) {
            state = ParserState::kDefault;
          } else if (byte == '%' || byte == '(' || byte == '<' ||
                     byte == '\\') {
            state = ParserState::kDefault;
            --i;
          } else if (inside_index == 6) {
            state = ParserState::kDefault;
            --i;
          } else if (byte == "endobj"[inside_index]) {
            inside_index++;
          }
          break;
      }

      if (bOverFlow) {
        size = 0;
        break;
      }
    }
    pos += size;

    // If the position has not changed at all or went backwards in a loop
    // iteration, then break out to prevent infinite looping.
    if (pos <= saved_pos)
      break;
  }

  if (last_xref != -1 && last_xref > last_obj)
    last_trailer = last_xref;
  else if (last_trailer == -1 || last_xref < last_obj)
    last_trailer = m_pSyntax->m_FileLen;

  m_SortedOffset.insert(last_trailer - m_pSyntax->m_HeaderOffset);
  return GetTrailer() && !m_ObjectInfo.empty();
}

bool CPDF_Parser::LoadCrossRefV5(FX_FILESIZE* pos, bool bMainXRef) {
  std::unique_ptr<CPDF_Object> pObject(
      ParseIndirectObjectAt(m_pDocument.Get(), *pos, 0));
  if (!pObject)
    return false;

  uint32_t objnum = pObject->m_ObjNum;
  if (!objnum)
    return false;

  CPDF_Object* pUnownedObject = pObject.get();
  if (m_pDocument) {
    CPDF_Dictionary* pRootDict = m_pDocument->GetRoot();
    if (pRootDict && pRootDict->GetObjNum() == objnum)
      return false;
    if (!m_pDocument->ReplaceIndirectObjectIfHigherGeneration(
            objnum, std::move(pObject))) {
      return false;
    }
  }

  CPDF_Stream* pStream = pUnownedObject->AsStream();
  if (!pStream)
    return false;

  CPDF_Dictionary* pDict = pStream->GetDict();
  *pos = pDict->GetIntegerFor("Prev");
  int32_t size = pDict->GetIntegerFor("Size");
  if (size < 0)
    return false;

  std::unique_ptr<CPDF_Dictionary> pNewTrailer = ToDictionary(pDict->Clone());
  if (bMainXRef) {
    m_Trailers.push_back(std::move(pNewTrailer));
    m_TrailerPos = m_Trailers.size() - 1;
    ShrinkObjectMap(size);
    for (auto& it : m_ObjectInfo)
      it.second.type = ObjectType::kFree;
  } else {
    m_Trailers.push_back(std::move(pNewTrailer));
  }

  std::vector<std::pair<int32_t, int32_t>> arrIndex;
  CPDF_Array* pArray = pDict->GetArrayFor("Index");
  if (pArray) {
    for (size_t i = 0; i < pArray->GetCount() / 2; i++) {
      CPDF_Object* pStartNumObj = pArray->GetObjectAt(i * 2);
      CPDF_Object* pCountObj = pArray->GetObjectAt(i * 2 + 1);

      if (ToNumber(pStartNumObj) && ToNumber(pCountObj)) {
        int nStartNum = pStartNumObj->GetInteger();
        int nCount = pCountObj->GetInteger();
        if (nStartNum >= 0 && nCount > 0)
          arrIndex.push_back(std::make_pair(nStartNum, nCount));
      }
    }
  }

  if (arrIndex.size() == 0)
    arrIndex.push_back(std::make_pair(0, size));

  pArray = pDict->GetArrayFor("W");
  if (!pArray)
    return false;

  std::vector<uint32_t> WidthArray;
  FX_SAFE_UINT32 dwAccWidth = 0;
  for (size_t i = 0; i < pArray->GetCount(); ++i) {
    WidthArray.push_back(pArray->GetIntegerAt(i));
    dwAccWidth += WidthArray[i];
  }

  if (!dwAccWidth.IsValid() || WidthArray.size() < 3)
    return false;

  uint32_t totalWidth = dwAccWidth.ValueOrDie();
  auto pAcc = pdfium::MakeRetain<CPDF_StreamAcc>(pStream);
  pAcc->LoadAllData();

  const uint8_t* pData = pAcc->GetData();
  uint32_t dwTotalSize = pAcc->GetSize();
  uint32_t segindex = 0;
  for (uint32_t i = 0; i < arrIndex.size(); i++) {
    int32_t startnum = arrIndex[i].first;
    if (startnum < 0)
      continue;

    m_dwXrefStartObjNum = pdfium::base::checked_cast<uint32_t>(startnum);
    uint32_t count = pdfium::base::checked_cast<uint32_t>(arrIndex[i].second);
    FX_SAFE_UINT32 dwCaculatedSize = segindex;
    dwCaculatedSize += count;
    dwCaculatedSize *= totalWidth;
    if (!dwCaculatedSize.IsValid() ||
        dwCaculatedSize.ValueOrDie() > dwTotalSize) {
      continue;
    }

    const uint8_t* segstart = pData + segindex * totalWidth;
    FX_SAFE_UINT32 dwMaxObjNum = startnum;
    dwMaxObjNum += count;
    uint32_t dwV5Size = m_ObjectInfo.empty() ? 0 : GetLastObjNum() + 1;
    if (!dwMaxObjNum.IsValid() || dwMaxObjNum.ValueOrDie() > dwV5Size)
      continue;

    for (uint32_t j = 0; j < count; j++) {
      ObjectType type = ObjectType::kNotCompressed;
      const uint8_t* entrystart = segstart + j * totalWidth;
      if (WidthArray[0]) {
        const int cross_ref_stream_obj_type =
            GetVarInt(entrystart, WidthArray[0]);
        type = GetObjectTypeFromCrossRefStreamType(cross_ref_stream_obj_type);
      }

      if (GetObjectType(startnum + j) == ObjectType::kNull) {
        FX_FILESIZE offset =
            GetVarInt(entrystart + WidthArray[0], WidthArray[1]);
        m_ObjectInfo[startnum + j].pos = offset;
        m_SortedOffset.insert(offset);
        continue;
      }

      if (GetObjectType(startnum + j) != ObjectType::kFree)
        continue;

      ObjectInfo& info = m_ObjectInfo[startnum + j];

      info.type = type;
      if (type == ObjectType::kFree) {
        info.pos = 0;
      } else {
        const FX_FILESIZE entry_value =
            GetVarInt(entrystart + WidthArray[0], WidthArray[1]);
        if (type == ObjectType::kNotCompressed) {
          const auto object_offset = entry_value;
          m_SortedOffset.insert(object_offset);
          info.pos = object_offset;
        } else {
          const auto archive_obj_num = entry_value;
          info.archive_obj_num = archive_obj_num;
          if (archive_obj_num < 0 || !IsValidObjectNumber(archive_obj_num))
            return false;
          m_ObjectInfo[archive_obj_num].type = ObjectType::kNull;
        }
      }
    }
    segindex += count;
  }
  return true;
}

CPDF_Array* CPDF_Parser::GetIDArray() {
  if (!GetTrailer())
    return nullptr;

  CPDF_Object* pID = GetTrailer()->GetObjectFor("ID");
  if (!pID)
    return nullptr;

  CPDF_Reference* pRef = pID->AsReference();
  if (!pRef)
    return ToArray(pID);

  std::unique_ptr<CPDF_Object> pNewObj =
      ParseIndirectObject(nullptr, pRef->GetRefObjNum());
  pID = pNewObj.get();
  GetTrailer()->SetFor("ID", std::move(pNewObj));
  return ToArray(pID);
}

uint32_t CPDF_Parser::GetRootObjNum() {
  CPDF_Reference* pRef =
      ToReference(GetTrailer() ? GetTrailer()->GetObjectFor("Root") : nullptr);
  return pRef ? pRef->GetRefObjNum() : 0;
}

uint32_t CPDF_Parser::GetInfoObjNum() {
  CPDF_Reference* pRef =
      ToReference(GetTrailer() ? GetTrailer()->GetObjectFor("Info") : nullptr);
  if (pRef)
    return pRef->GetRefObjNum();

  // Search trailers array from latest to earliest revision, as we want the
  // most recent Info object number.
  // See PDF 1.7 spec, section 3.4.5 - Incremental Updates.
  for (auto it = m_Trailers.rbegin(); it != m_Trailers.rend(); ++it) {
    pRef = ToReference(it->get()->GetObjectFor("Info"));
    if (pRef)
      return pRef->GetRefObjNum();
  }
  return 0;
}

std::unique_ptr<CPDF_Object> CPDF_Parser::ParseIndirectObject(
    CPDF_IndirectObjectHolder* pObjList,
    uint32_t objnum) {
  if (!IsValidObjectNumber(objnum))
    return nullptr;

  // Prevent circular parsing the same object.
  if (pdfium::ContainsKey(m_ParsingObjNums, objnum))
    return nullptr;

  pdfium::ScopedSetInsertion<uint32_t> local_insert(&m_ParsingObjNums, objnum);
  if (GetObjectType(objnum) == ObjectType::kNotCompressed ||
      GetObjectType(objnum) == ObjectType::kNull) {
    FX_FILESIZE pos = m_ObjectInfo[objnum].pos;
    if (pos <= 0)
      return nullptr;
    return ParseIndirectObjectAt(pObjList, pos, objnum);
  }
  if (GetObjectType(objnum) != ObjectType::kCompressed)
    return nullptr;

  CFX_RetainPtr<CPDF_StreamAcc> pObjStream =
      GetObjectStream(m_ObjectInfo[objnum].pos);
  if (!pObjStream)
    return nullptr;

  auto file = pdfium::MakeRetain<CFX_MemoryStream>(
      const_cast<uint8_t*>(pObjStream->GetData()),
      static_cast<size_t>(pObjStream->GetSize()), false);
  CPDF_SyntaxParser syntax;
  syntax.InitParser(file, 0);
  const int32_t offset = GetStreamFirst(pObjStream);

  // Read object numbers from |pObjStream| into a cache.
  if (!pdfium::ContainsKey(m_ObjCache, pObjStream)) {
    for (int32_t i = GetStreamNCount(pObjStream); i > 0; --i) {
      uint32_t thisnum = syntax.GetDirectNum();
      uint32_t thisoff = syntax.GetDirectNum();
      m_ObjCache[pObjStream][thisnum] = thisoff;
    }
  }

  const auto it = m_ObjCache[pObjStream].find(objnum);
  if (it == m_ObjCache[pObjStream].end())
    return nullptr;

  syntax.SetPos(offset + it->second);
  return syntax.GetObject(pObjList, 0, 0, true);
}

CFX_RetainPtr<CPDF_StreamAcc> CPDF_Parser::GetObjectStream(uint32_t objnum) {
  auto it = m_ObjectStreamMap.find(objnum);
  if (it != m_ObjectStreamMap.end())
    return it->second;

  if (!m_pDocument)
    return nullptr;

  const CPDF_Stream* pStream =
      ToStream(m_pDocument->GetOrParseIndirectObject(objnum));
  if (!pStream)
    return nullptr;

  auto pStreamAcc = pdfium::MakeRetain<CPDF_StreamAcc>(pStream);
  pStreamAcc->LoadAllData();
  m_ObjectStreamMap[objnum] = pStreamAcc;
  return pStreamAcc;
}

FX_FILESIZE CPDF_Parser::GetObjectSize(uint32_t objnum) const {
  if (!IsValidObjectNumber(objnum))
    return 0;

  if (GetObjectType(objnum) == ObjectType::kCompressed)
    objnum = GetObjectPositionOrZero(objnum);

  if (GetObjectType(objnum) != ObjectType::kNotCompressed &&
      GetObjectType(objnum) != ObjectType::kNull)
    return 0;

  FX_FILESIZE offset = GetObjectPositionOrZero(objnum);
  if (offset == 0)
    return 0;

  auto it = m_SortedOffset.find(offset);
  if (it == m_SortedOffset.end() || ++it == m_SortedOffset.end())
    return 0;

  return *it - offset;
}

std::vector<uint8_t> CPDF_Parser::GetIndirectBinary(uint32_t objnum) {
  std::vector<uint8_t> buffer;
  if (!IsValidObjectNumber(objnum))
    return buffer;

  if (GetObjectType(objnum) == ObjectType::kCompressed) {
    CFX_RetainPtr<CPDF_StreamAcc> pObjStream =
        GetObjectStream(m_ObjectInfo[objnum].pos);
    if (!pObjStream)
      return buffer;

    int32_t offset = GetStreamFirst(pObjStream);
    const uint8_t* pData = pObjStream->GetData();
    uint32_t totalsize = pObjStream->GetSize();
    auto file = pdfium::MakeRetain<CFX_MemoryStream>(
        const_cast<uint8_t*>(pData), static_cast<size_t>(totalsize), false);
    CPDF_SyntaxParser syntax;
    syntax.InitParser(file, 0);

    for (int i = GetStreamNCount(pObjStream); i > 0; --i) {
      uint32_t thisnum = syntax.GetDirectNum();
      uint32_t thisoff = syntax.GetDirectNum();
      if (thisnum != objnum)
        continue;

      size_t size = 0;
      if (i == 1) {
        size = totalsize - (thisoff + offset);
      } else {
        syntax.GetDirectNum();  // Skip nextnum.
        uint32_t nextoff = syntax.GetDirectNum();
        size = nextoff - thisoff;
      }

      buffer.resize(size);
      memcpy(buffer.data(), pData + thisoff + offset, size);
      break;
    }
    return buffer;
  }

  if (GetObjectType(objnum) != ObjectType::kNotCompressed)
    return buffer;

  FX_FILESIZE pos = m_ObjectInfo[objnum].pos;
  if (pos == 0)
    return buffer;

  FX_FILESIZE SavedPos = m_pSyntax->GetPos();
  m_pSyntax->SetPos(pos);

  bool bIsNumber;
  CFX_ByteString word = m_pSyntax->GetNextWord(&bIsNumber);
  if (!bIsNumber) {
    m_pSyntax->SetPos(SavedPos);
    return buffer;
  }

  uint32_t parser_objnum = FXSYS_atoui(word.c_str());
  if (parser_objnum && parser_objnum != objnum) {
    m_pSyntax->SetPos(SavedPos);
    return buffer;
  }

  word = m_pSyntax->GetNextWord(&bIsNumber);
  if (!bIsNumber) {
    m_pSyntax->SetPos(SavedPos);
    return buffer;
  }

  if (m_pSyntax->GetKeyword() != "obj") {
    m_pSyntax->SetPos(SavedPos);
    return buffer;
  }

  auto it = m_SortedOffset.find(pos);
  if (it == m_SortedOffset.end() || ++it == m_SortedOffset.end()) {
    m_pSyntax->SetPos(SavedPos);
    return buffer;
  }

  FX_FILESIZE nextoff = *it;
  bool bNextOffValid = false;
  if (nextoff != pos) {
    m_pSyntax->SetPos(nextoff);
    word = m_pSyntax->GetNextWord(&bIsNumber);
    if (word == "xref") {
      bNextOffValid = true;
    } else if (bIsNumber) {
      word = m_pSyntax->GetNextWord(&bIsNumber);
      if (bIsNumber && m_pSyntax->GetKeyword() == "obj") {
        bNextOffValid = true;
      }
    }
  }

  if (!bNextOffValid) {
    m_pSyntax->SetPos(pos);
    while (1) {
      if (m_pSyntax->GetKeyword() == "endobj")
        break;

      if (m_pSyntax->GetPos() == m_pSyntax->m_FileLen)
        break;
    }
    nextoff = m_pSyntax->GetPos();
  }

  size_t size = (uint32_t)(nextoff - pos);
  buffer.resize(size);
  m_pSyntax->SetPos(pos);
  m_pSyntax->ReadBlock(buffer.data(), size);
  m_pSyntax->SetPos(SavedPos);
  return buffer;
}

std::unique_ptr<CPDF_Object> CPDF_Parser::ParseIndirectObjectAt(
    CPDF_IndirectObjectHolder* pObjList,
    FX_FILESIZE pos,
    uint32_t objnum) {
  FX_FILESIZE SavedPos = m_pSyntax->GetPos();
  m_pSyntax->SetPos(pos);
  bool bIsNumber;
  CFX_ByteString word = m_pSyntax->GetNextWord(&bIsNumber);
  if (!bIsNumber) {
    m_pSyntax->SetPos(SavedPos);
    return nullptr;
  }

  FX_FILESIZE objOffset = m_pSyntax->GetPos();
  objOffset -= word.GetLength();
  uint32_t parser_objnum = FXSYS_atoui(word.c_str());
  if (objnum && parser_objnum != objnum) {
    m_pSyntax->SetPos(SavedPos);
    return nullptr;
  }

  word = m_pSyntax->GetNextWord(&bIsNumber);
  if (!bIsNumber) {
    m_pSyntax->SetPos(SavedPos);
    return nullptr;
  }

  uint32_t parser_gennum = FXSYS_atoui(word.c_str());
  if (m_pSyntax->GetKeyword() != "obj") {
    m_pSyntax->SetPos(SavedPos);
    return nullptr;
  }

  std::unique_ptr<CPDF_Object> pObj =
      m_pSyntax->GetObject(pObjList, objnum, parser_gennum, true);
  m_pSyntax->GetPos();

  CFX_ByteString bsWord = m_pSyntax->GetKeyword();
  if (bsWord == "endobj")
    m_pSyntax->GetPos();

  m_pSyntax->SetPos(SavedPos);
  if (pObj) {
    if (!objnum)
      pObj->m_ObjNum = parser_objnum;
    pObj->m_GenNum = parser_gennum;
  }
  return pObj;
}

std::unique_ptr<CPDF_Object> CPDF_Parser::ParseIndirectObjectAtByStrict(
    CPDF_IndirectObjectHolder* pObjList,
    FX_FILESIZE pos,
    uint32_t objnum,
    FX_FILESIZE* pResultPos) {
  FX_FILESIZE SavedPos = m_pSyntax->GetPos();
  m_pSyntax->SetPos(pos);

  bool bIsNumber;
  CFX_ByteString word = m_pSyntax->GetNextWord(&bIsNumber);
  if (!bIsNumber) {
    m_pSyntax->SetPos(SavedPos);
    return nullptr;
  }

  uint32_t parser_objnum = FXSYS_atoui(word.c_str());
  if (objnum && parser_objnum != objnum) {
    m_pSyntax->SetPos(SavedPos);
    return nullptr;
  }

  word = m_pSyntax->GetNextWord(&bIsNumber);
  if (!bIsNumber) {
    m_pSyntax->SetPos(SavedPos);
    return nullptr;
  }

  uint32_t gennum = FXSYS_atoui(word.c_str());
  if (m_pSyntax->GetKeyword() != "obj") {
    m_pSyntax->SetPos(SavedPos);
    return nullptr;
  }

  std::unique_ptr<CPDF_Object> pObj =
      m_pSyntax->GetObjectForStrict(pObjList, objnum, gennum);

  if (pResultPos)
    *pResultPos = m_pSyntax->m_Pos;

  m_pSyntax->SetPos(SavedPos);
  return pObj;
}

uint32_t CPDF_Parser::GetFirstPageNo() const {
  return m_pLinearized ? m_pLinearized->GetFirstPageNo() : 0;
}

std::unique_ptr<CPDF_Dictionary> CPDF_Parser::LoadTrailerV4() {
  if (m_pSyntax->GetKeyword() != "trailer")
    return nullptr;

  return ToDictionary(m_pSyntax->GetObject(m_pDocument.Get(), 0, 0, true));
}

uint32_t CPDF_Parser::GetPermissions() const {
  if (!m_pSecurityHandler)
    return 0xFFFFFFFF;

  uint32_t dwPermission = m_pSecurityHandler->GetPermissions();
  if (m_pEncryptDict && m_pEncryptDict->GetStringFor("Filter") == "Standard") {
    // See PDF Reference 1.7, page 123, table 3.20.
    dwPermission &= 0xFFFFFFFC;
    dwPermission |= 0xFFFFF0C0;
  }
  return dwPermission;
}

bool CPDF_Parser::IsLinearizedFile(
    const CFX_RetainPtr<IFX_SeekableReadStream>& pFileAccess,
    uint32_t offset) {
  m_pSyntax->InitParser(pFileAccess, offset);
  m_pSyntax->SetPos(m_pSyntax->m_HeaderOffset + 9);

  FX_FILESIZE SavedPos = m_pSyntax->GetPos();
  bool bIsNumber;
  CFX_ByteString word = m_pSyntax->GetNextWord(&bIsNumber);
  if (!bIsNumber)
    return false;

  uint32_t objnum = FXSYS_atoui(word.c_str());
  word = m_pSyntax->GetNextWord(&bIsNumber);
  if (!bIsNumber)
    return false;

  uint32_t gennum = FXSYS_atoui(word.c_str());
  if (m_pSyntax->GetKeyword() != "obj") {
    m_pSyntax->SetPos(SavedPos);
    return false;
  }

  m_pLinearized = CPDF_LinearizedHeader::CreateForObject(
      m_pSyntax->GetObject(nullptr, objnum, gennum, true));
  if (!m_pLinearized)
    return false;

  m_LastXRefOffset = m_pLinearized->GetLastXRefOffset();
  // Move parser onto first page xref table start.
  m_pSyntax->GetNextWord(nullptr);
  return true;
}

CPDF_Parser::Error CPDF_Parser::StartLinearizedParse(
    const CFX_RetainPtr<IFX_SeekableReadStream>& pFileAccess,
    CPDF_Document* pDocument) {
  ASSERT(!m_bHasParsed);
  m_bXRefStream = false;
  m_LastXRefOffset = 0;

  int32_t offset = GetHeaderOffset(pFileAccess);
  if (offset == -1)
    return FORMAT_ERROR;

  if (!IsLinearizedFile(pFileAccess, offset)) {
    m_pSyntax->m_pFileAccess = nullptr;
    return StartParse(pFileAccess, std::move(pDocument));
  }
  m_bHasParsed = true;
  m_pDocument = pDocument;

  FX_FILESIZE dwFirstXRefOffset = m_pSyntax->GetPos();
  bool bXRefRebuilt = false;
  bool bLoadV4 = LoadCrossRefV4(dwFirstXRefOffset, 0, false);
  if (!bLoadV4 && !LoadCrossRefV5(&dwFirstXRefOffset, true)) {
    if (!RebuildCrossRef())
      return FORMAT_ERROR;

    bXRefRebuilt = true;
    m_LastXRefOffset = 0;
  }

  if (bLoadV4) {
    std::unique_ptr<CPDF_Dictionary> trailer = LoadTrailerV4();
    if (!trailer)
      return SUCCESS;

    m_Trailers.push_back(std::move(trailer));
    m_TrailerPos = m_Trailers.size() - 1;
    int32_t xrefsize = GetDirectInteger(GetTrailer(), "Size");
    if (xrefsize > 0)
      ShrinkObjectMap(xrefsize);
  }

  Error eRet = SetEncryptHandler();
  if (eRet != SUCCESS)
    return eRet;

  m_pDocument->LoadLinearizedDoc(m_pLinearized.get());
  if (!m_pDocument->GetRoot() || m_pDocument->GetPageCount() == 0) {
    if (bXRefRebuilt)
      return FORMAT_ERROR;

    ReleaseEncryptHandler();
    if (!RebuildCrossRef())
      return FORMAT_ERROR;

    eRet = SetEncryptHandler();
    if (eRet != SUCCESS)
      return eRet;

    m_pDocument->LoadLinearizedDoc(m_pLinearized.get());
    if (!m_pDocument->GetRoot())
      return FORMAT_ERROR;
  }

  if (GetRootObjNum() == 0) {
    ReleaseEncryptHandler();
    if (!RebuildCrossRef() || GetRootObjNum() == 0)
      return FORMAT_ERROR;

    eRet = SetEncryptHandler();
    if (eRet != SUCCESS)
      return eRet;
  }

  if (m_pSecurityHandler && m_pSecurityHandler->IsMetadataEncrypted()) {
    if (CPDF_Reference* pMetadata =
            ToReference(m_pDocument->GetRoot()->GetObjectFor("Metadata")))
      m_pSyntax->m_MetadataObjnum = pMetadata->GetRefObjNum();
  }
  return SUCCESS;
}

bool CPDF_Parser::LoadLinearizedAllCrossRefV5(FX_FILESIZE xrefpos) {
  if (!LoadCrossRefV5(&xrefpos, false))
    return false;

  std::set<FX_FILESIZE> seen_xrefpos;
  while (xrefpos) {
    seen_xrefpos.insert(xrefpos);
    if (!LoadCrossRefV5(&xrefpos, false))
      return false;

    // Check for circular references.
    if (pdfium::ContainsKey(seen_xrefpos, xrefpos))
      return false;
  }
  m_ObjectStreamMap.clear();
  m_bXRefStream = true;
  return true;
}

CPDF_Parser::Error CPDF_Parser::LoadLinearizedMainXRefTable() {
  uint32_t dwSaveMetadataObjnum = m_pSyntax->m_MetadataObjnum;
  m_pSyntax->m_MetadataObjnum = 0;
  m_pSyntax->SetPos(m_LastXRefOffset - m_pSyntax->m_HeaderOffset);

  uint8_t ch = 0;
  uint32_t dwCount = 0;
  m_pSyntax->GetNextChar(ch);
  while (PDFCharIsWhitespace(ch)) {
    ++dwCount;
    if (m_pSyntax->m_FileLen <=
        (FX_FILESIZE)(m_pSyntax->GetPos() + m_pSyntax->m_HeaderOffset)) {
      break;
    }
    m_pSyntax->GetNextChar(ch);
  }
  m_LastXRefOffset += dwCount;
  m_ObjectStreamMap.clear();
  m_ObjCache.clear();

  if (!LoadLinearizedAllCrossRefV4(m_LastXRefOffset, m_dwXrefStartObjNum) &&
      !LoadLinearizedAllCrossRefV5(m_LastXRefOffset)) {
    m_LastXRefOffset = 0;
    m_pSyntax->m_MetadataObjnum = dwSaveMetadataObjnum;
    return FORMAT_ERROR;
  }

  m_pSyntax->m_MetadataObjnum = dwSaveMetadataObjnum;
  return SUCCESS;
}
