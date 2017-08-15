// Copyright 2014 PDFium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Original code copyright 2014 Foxit Software Inc. http://www.foxitsoftware.com

#ifndef FPDFSDK_PWL_CPWL_EDIT_IMPL_H_
#define FPDFSDK_PWL_CPWL_EDIT_IMPL_H_

#include <deque>
#include <memory>
#include <vector>

#include "core/fpdfdoc/cpvt_secprops.h"
#include "core/fpdfdoc/cpvt_wordprops.h"
#include "core/fxcrt/cfx_unowned_ptr.h"

#define FX_EDIT_ISLATINWORD(u)                  \
  (u == 0x2D || (u <= 0x005A && u >= 0x0041) || \
   (u <= 0x007A && u >= 0x0061) || (u <= 0x02AF && u >= 0x00C0))

class CFFL_FormFiller;
class CPWL_EditImpl;
class CPWL_EditImpl_Iterator;
class CPWL_EditImpl_Provider;
class CFX_RenderDevice;
class CFX_SystemHandler;
class CPWL_Edit;
class CPWL_EditCtrl;
class IFX_Edit_UndoItem;

struct CPWL_EditImpl_LineRect {
  CPWL_EditImpl_LineRect(const CPVT_WordRange& wrLine,
                         const CFX_FloatRect& rcLine)
      : m_wrLine(wrLine), m_rcLine(rcLine) {}

  CPVT_WordRange m_wrLine;
  CFX_FloatRect m_rcLine;
};

class CPWL_EditImpl_LineRectArray {
 public:
  CPWL_EditImpl_LineRectArray();
  ~CPWL_EditImpl_LineRectArray();

  void operator=(CPWL_EditImpl_LineRectArray&& rects);
  void Add(const CPVT_WordRange& wrLine, const CFX_FloatRect& rcLine);

  int32_t GetSize() const;
  CPWL_EditImpl_LineRect* GetAt(int32_t nIndex) const;

 private:
  std::vector<std::unique_ptr<CPWL_EditImpl_LineRect>> m_LineRects;
};

class CPWL_EditImpl_RectArray {
 public:
  CPWL_EditImpl_RectArray();
  ~CPWL_EditImpl_RectArray();

  void Clear();
  void Add(const CFX_FloatRect& rect);

  int32_t GetSize() const;
  CFX_FloatRect* GetAt(int32_t nIndex) const;

 private:
  std::vector<std::unique_ptr<CFX_FloatRect>> m_Rects;
};

class CPWL_EditImpl_Refresh {
 public:
  CPWL_EditImpl_Refresh();
  ~CPWL_EditImpl_Refresh();

  void BeginRefresh();
  void Push(const CPVT_WordRange& linerange, const CFX_FloatRect& rect);
  void NoAnalyse();
  const CPWL_EditImpl_RectArray* GetRefreshRects() const;
  void EndRefresh();

 private:
  CPWL_EditImpl_LineRectArray m_NewLineRects;
  CPWL_EditImpl_LineRectArray m_OldLineRects;
  CPWL_EditImpl_RectArray m_RefreshRects;
};

class CPWL_EditImpl_Select {
 public:
  CPWL_EditImpl_Select();
  explicit CPWL_EditImpl_Select(const CPVT_WordRange& range);

  void Reset();
  void Set(const CPVT_WordPlace& begin, const CPVT_WordPlace& end);
  void SetEndPos(const CPVT_WordPlace& end);

  CPVT_WordRange ConvertToWordRange() const;
  bool IsEmpty() const;

  CPVT_WordPlace BeginPos;
  CPVT_WordPlace EndPos;
};

class CPWL_EditImpl_Undo {
 public:
  CPWL_EditImpl_Undo();
  ~CPWL_EditImpl_Undo();

  void AddItem(std::unique_ptr<IFX_Edit_UndoItem> pItem);
  void Undo();
  void Redo();
  bool CanUndo() const;
  bool CanRedo() const;

 private:
  void RemoveHeads();
  void RemoveTails();

  std::deque<std::unique_ptr<IFX_Edit_UndoItem>> m_UndoItemStack;
  size_t m_nCurUndoPos;
  bool m_bWorking;
};

class IFX_Edit_UndoItem {
 public:
  virtual ~IFX_Edit_UndoItem() {}

  virtual void Undo() = 0;
  virtual void Redo() = 0;
};

class CFXEU_InsertWord : public IFX_Edit_UndoItem {
 public:
  CFXEU_InsertWord(CPWL_EditImpl* pEdit,
                   const CPVT_WordPlace& wpOldPlace,
                   const CPVT_WordPlace& wpNewPlace,
                   uint16_t word,
                   int32_t charset,
                   const CPVT_WordProps* pWordProps);
  ~CFXEU_InsertWord() override;

  // IFX_Edit_UndoItem:
  void Redo() override;
  void Undo() override;

 private:
  CFX_UnownedPtr<CPWL_EditImpl> m_pEdit;

  CPVT_WordPlace m_wpOld;
  CPVT_WordPlace m_wpNew;
  uint16_t m_Word;
  int32_t m_nCharset;
  CPVT_WordProps m_WordProps;
};

class CFXEU_InsertReturn : public IFX_Edit_UndoItem {
 public:
  CFXEU_InsertReturn(CPWL_EditImpl* pEdit,
                     const CPVT_WordPlace& wpOldPlace,
                     const CPVT_WordPlace& wpNewPlace,
                     const CPVT_SecProps* pSecProps,
                     const CPVT_WordProps* pWordProps);
  ~CFXEU_InsertReturn() override;

  // IFX_Edit_UndoItem:
  void Redo() override;
  void Undo() override;

 private:
  CFX_UnownedPtr<CPWL_EditImpl> m_pEdit;

  CPVT_WordPlace m_wpOld;
  CPVT_WordPlace m_wpNew;
  CPVT_SecProps m_SecProps;
  CPVT_WordProps m_WordProps;
};

class CFXEU_Backspace : public IFX_Edit_UndoItem {
 public:
  CFXEU_Backspace(CPWL_EditImpl* pEdit,
                  const CPVT_WordPlace& wpOldPlace,
                  const CPVT_WordPlace& wpNewPlace,
                  uint16_t word,
                  int32_t charset,
                  const CPVT_SecProps& SecProps,
                  const CPVT_WordProps& WordProps);
  ~CFXEU_Backspace() override;

  // IFX_Edit_UndoItem:
  void Redo() override;
  void Undo() override;

 private:
  CFX_UnownedPtr<CPWL_EditImpl> m_pEdit;

  CPVT_WordPlace m_wpOld;
  CPVT_WordPlace m_wpNew;
  uint16_t m_Word;
  int32_t m_nCharset;
  CPVT_SecProps m_SecProps;
  CPVT_WordProps m_WordProps;
};

class CFXEU_Delete : public IFX_Edit_UndoItem {
 public:
  CFXEU_Delete(CPWL_EditImpl* pEdit,
               const CPVT_WordPlace& wpOldPlace,
               const CPVT_WordPlace& wpNewPlace,
               uint16_t word,
               int32_t charset,
               const CPVT_SecProps& SecProps,
               const CPVT_WordProps& WordProps,
               bool bSecEnd);
  ~CFXEU_Delete() override;

  // IFX_Edit_UndoItem:
  void Redo() override;
  void Undo() override;

 private:
  CFX_UnownedPtr<CPWL_EditImpl> m_pEdit;

  CPVT_WordPlace m_wpOld;
  CPVT_WordPlace m_wpNew;
  uint16_t m_Word;
  int32_t m_nCharset;
  CPVT_SecProps m_SecProps;
  CPVT_WordProps m_WordProps;
  bool m_bSecEnd;
};

class CFXEU_Clear : public IFX_Edit_UndoItem {
 public:
  CFXEU_Clear(CPWL_EditImpl* pEdit,
              const CPVT_WordRange& wrSel,
              const CFX_WideString& swText);
  ~CFXEU_Clear() override;

  // IFX_Edit_UndoItem:
  void Redo() override;
  void Undo() override;

 private:
  CFX_UnownedPtr<CPWL_EditImpl> m_pEdit;

  CPVT_WordRange m_wrSel;
  CFX_WideString m_swText;
};

class CFXEU_InsertText : public IFX_Edit_UndoItem {
 public:
  CFXEU_InsertText(CPWL_EditImpl* pEdit,
                   const CPVT_WordPlace& wpOldPlace,
                   const CPVT_WordPlace& wpNewPlace,
                   const CFX_WideString& swText,
                   int32_t charset);
  ~CFXEU_InsertText() override;

  // IFX_Edit_UndoItem:
  void Redo() override;
  void Undo() override;

 private:
  CFX_UnownedPtr<CPWL_EditImpl> m_pEdit;

  CPVT_WordPlace m_wpOld;
  CPVT_WordPlace m_wpNew;
  CFX_WideString m_swText;
  int32_t m_nCharset;
};

class CPWL_EditImpl {
 public:
  static void DrawEdit(CFX_RenderDevice* pDevice,
                       CFX_Matrix* pUser2Device,
                       CPWL_EditImpl* pEdit,
                       FX_COLORREF crTextFill,
                       const CFX_FloatRect& rcClip,
                       const CFX_PointF& ptOffset,
                       const CPVT_WordRange* pRange,
                       CFX_SystemHandler* pSystemHandler,
                       CFFL_FormFiller* pFFLData);

  CPWL_EditImpl();
  ~CPWL_EditImpl();

  void SetFontMap(IPVT_FontMap* pFontMap);
  void SetNotify(CPWL_EditCtrl* pNotify);
  void SetOperationNotify(CPWL_Edit* pOperationNotify);

  // Returns an iterator for the contents. Should not be released.
  CPWL_EditImpl_Iterator* GetIterator();
  IPVT_FontMap* GetFontMap();
  void Initialize();

  // Set the bounding box of the text area.
  void SetPlateRect(const CFX_FloatRect& rect);
  void SetScrollPos(const CFX_PointF& point);

  // Set the horizontal text alignment. (nFormat [0:left, 1:middle, 2:right])
  void SetAlignmentH(int32_t nFormat, bool bPaint);
  // Set the vertical text alignment. (nFormat [0:left, 1:middle, 2:right])
  void SetAlignmentV(int32_t nFormat, bool bPaint);

  // Set the substitution character for hidden text.
  void SetPasswordChar(uint16_t wSubWord, bool bPaint);

  // Set the maximum number of words in the text.
  void SetLimitChar(int32_t nLimitChar);
  void SetCharArray(int32_t nCharArray);
  void SetCharSpace(float fCharSpace);
  void SetMultiLine(bool bMultiLine, bool bPaint);
  void SetAutoReturn(bool bAuto, bool bPaint);
  void SetAutoFontSize(bool bAuto, bool bPaint);
  void SetAutoScroll(bool bAuto, bool bPaint);
  void SetFontSize(float fFontSize);
  void SetTextOverflow(bool bAllowed, bool bPaint);
  void OnMouseDown(const CFX_PointF& point, bool bShift, bool bCtrl);
  void OnMouseMove(const CFX_PointF& point, bool bShift, bool bCtrl);
  void OnVK_UP(bool bShift, bool bCtrl);
  void OnVK_DOWN(bool bShift, bool bCtrl);
  void OnVK_LEFT(bool bShift, bool bCtrl);
  void OnVK_RIGHT(bool bShift, bool bCtrl);
  void OnVK_HOME(bool bShift, bool bCtrl);
  void OnVK_END(bool bShift, bool bCtrl);
  void SetText(const CFX_WideString& sText);
  bool InsertWord(uint16_t word, int32_t charset);
  bool InsertReturn();
  bool Backspace();
  bool Delete();
  bool ClearSelection();
  bool InsertText(const CFX_WideString& sText, int32_t charset);
  bool Redo();
  bool Undo();
  CPVT_WordPlace WordIndexToWordPlace(int32_t index) const;
  CPVT_WordPlace SearchWordPlace(const CFX_PointF& point) const;
  int32_t GetCaret() const;
  CPVT_WordPlace GetCaretWordPlace() const;
  CFX_WideString GetSelectedText() const;
  CFX_WideString GetText() const;
  float GetFontSize() const;
  uint16_t GetPasswordChar() const;
  CFX_PointF GetScrollPos() const;
  int32_t GetCharArray() const;
  CFX_FloatRect GetContentRect() const;
  CFX_WideString GetRangeText(const CPVT_WordRange& range) const;
  int32_t GetHorzScale() const;
  float GetCharSpace() const;
  void SetSelection(int32_t nStartChar, int32_t nEndChar);
  void GetSelection(int32_t& nStartChar, int32_t& nEndChar) const;
  void SelectAll();
  void SelectNone();
  bool IsSelected() const;
  void Paint();
  void EnableRefresh(bool bRefresh);
  void RefreshWordRange(const CPVT_WordRange& wr);
  CPVT_WordRange GetWholeWordRange() const;
  CPVT_WordRange GetSelectWordRange() const;
  void EnableUndo(bool bUndo);
  bool IsTextFull() const;
  bool IsTextOverflow() const;
  bool CanUndo() const;
  bool CanRedo() const;
  CPVT_WordRange GetVisibleWordRange() const;

  bool Empty();

  CPVT_WordPlace DoInsertText(const CPVT_WordPlace& place,
                              const CFX_WideString& sText,
                              int32_t charset);
  int32_t GetCharSetFromUnicode(uint16_t word, int32_t nOldCharset);

  int32_t GetTotalLines() const;

  CFX_ByteString GetPDFWordString(int32_t nFontIndex,
                                  uint16_t Word,
                                  uint16_t SubWord);

 private:
  friend class CPWL_EditImpl_Iterator;
  friend class CFXEU_InsertWord;
  friend class CFXEU_InsertReturn;
  friend class CFXEU_Backspace;
  friend class CFXEU_Delete;
  friend class CFXEU_Clear;
  friend class CFXEU_InsertText;

  void SetSelection(const CPVT_WordPlace& begin, const CPVT_WordPlace& end);

  void RearrangeAll();
  void RearrangePart(const CPVT_WordRange& range);
  void ScrollToCaret();
  void SetScrollInfo();
  void SetScrollPosX(float fx);
  void SetScrollPosY(float fy);
  void SetScrollLimit();
  void SetContentChanged();

  bool InsertWord(uint16_t word,
                  int32_t charset,
                  const CPVT_WordProps* pWordProps,
                  bool bAddUndo,
                  bool bPaint);
  bool InsertReturn(const CPVT_SecProps* pSecProps,
                    const CPVT_WordProps* pWordProps,
                    bool bAddUndo,
                    bool bPaint);
  bool Backspace(bool bAddUndo, bool bPaint);
  bool Delete(bool bAddUndo, bool bPaint);
  bool Clear(bool bAddUndo, bool bPaint);
  bool InsertText(const CFX_WideString& sText,
                  int32_t charset,
                  bool bAddUndo,
                  bool bPaint);
  void PaintInsertText(const CPVT_WordPlace& wpOld,
                       const CPVT_WordPlace& wpNew);

  inline CFX_PointF VTToEdit(const CFX_PointF& point) const;
  inline CFX_PointF EditToVT(const CFX_PointF& point) const;
  inline CFX_FloatRect VTToEdit(const CFX_FloatRect& rect) const;

  void Refresh();
  void RefreshPushLineRects(const CPVT_WordRange& wr);

  void SetCaret(const CPVT_WordPlace& place);
  void SetCaretInfo();
  void SetCaretOrigin();

  void AddEditUndoItem(std::unique_ptr<IFX_Edit_UndoItem> pEditUndoItem);

 private:
  std::unique_ptr<CPDF_VariableText> m_pVT;
  CFX_UnownedPtr<CPWL_EditCtrl> m_pNotify;
  CFX_UnownedPtr<CPWL_Edit> m_pOperationNotify;
  std::unique_ptr<CPWL_EditImpl_Provider> m_pVTProvider;
  CPVT_WordPlace m_wpCaret;
  CPVT_WordPlace m_wpOldCaret;
  CPWL_EditImpl_Select m_SelState;
  CFX_PointF m_ptScrollPos;
  CFX_PointF m_ptRefreshScrollPos;
  bool m_bEnableScroll;
  std::unique_ptr<CPWL_EditImpl_Iterator> m_pIterator;
  CPWL_EditImpl_Refresh m_Refresh;
  CFX_PointF m_ptCaret;
  CPWL_EditImpl_Undo m_Undo;
  int32_t m_nAlignment;
  bool m_bNotifyFlag;
  bool m_bEnableOverflow;
  bool m_bEnableRefresh;
  CFX_FloatRect m_rcOldContent;
  bool m_bEnableUndo;
};

class CPWL_EditImpl_Iterator {
 public:
  CPWL_EditImpl_Iterator(CPWL_EditImpl* pEdit,
                         CPDF_VariableText::Iterator* pVTIterator);
  ~CPWL_EditImpl_Iterator();

  bool NextWord();
  bool PrevWord();
  bool GetWord(CPVT_Word& word) const;
  bool GetLine(CPVT_Line& line) const;
  void SetAt(int32_t nWordIndex);
  void SetAt(const CPVT_WordPlace& place);
  const CPVT_WordPlace& GetAt() const;

 private:
  CFX_UnownedPtr<CPWL_EditImpl> m_pEdit;
  CPDF_VariableText::Iterator* m_pVTIterator;
};

class CPWL_EditImpl_Provider : public CPDF_VariableText::Provider {
 public:
  explicit CPWL_EditImpl_Provider(IPVT_FontMap* pFontMap);
  ~CPWL_EditImpl_Provider() override;

  IPVT_FontMap* GetFontMap() const;

  // CPDF_VariableText::Provider:
  int32_t GetCharWidth(int32_t nFontIndex, uint16_t word) override;
  int32_t GetTypeAscent(int32_t nFontIndex) override;
  int32_t GetTypeDescent(int32_t nFontIndex) override;
  int32_t GetWordFontIndex(uint16_t word,
                           int32_t charset,
                           int32_t nFontIndex) override;
  int32_t GetDefaultFontIndex() override;
  bool IsLatinWord(uint16_t word) override;

 private:
  IPVT_FontMap* m_pFontMap;
};

#endif  // FPDFSDK_PWL_CPWL_EDIT_IMPL_H_
