#pragma once

struct BinCheckBox
{
    int nID;
    int nParentID;
    int nTextureSetIndex;
    int nStartX;
    int nStartY;
    int nWidth;
    int nHeight;
    int nColor;
};

struct BinListBox
{
    int nID;
    int nParentID;
    int nTextureSetIndex;
    int nMaxCount;
    int nVisibleCount;
    int nStartX;
    int nStartY;
    int nWidth;
    int nHeight;
    int nColor;
    int nFillType;
    int nSelect;
    int nScroll;
};

struct BinGrid
{
    int nID;
    int nParentID;
    int nTextureSetIndex;
    int nStartX;
    int nStartY;
    int nWidth;
    int nHeight;
    int nRowCount;
    int nColumnCount;
    int nType;
};

struct BinPanel
{
    int nID;
    int nParentID;
    int nTextureSetIndex;
    int nStartX;
    int nStartY;
    int nWidth;
    int nHeight;
    int nColor;
    int nFillType;
    int nPickable;
};

struct Bin3DObj
{
    int nID;
    int nParentID;
    int n3DObjIndex;
    int nStartX;
    int nStartY;
    int nWidth;
    int nHeight;
};

struct BinButton
{
    int nID;
    int nParentID;
    int nTextureSetIndex;
    int nStartX;
    int nStartY;
    int nWidth;
    int nHeight;
    int nColor;
    int nSound;
    int nStringIndex;
};

struct BinText
{
    int nID;
    int nParentID;
    int nTextureSetIndex;
    int nStartX;
    int nStartY;
    int nWidth;
    int nHeight;
    int nFontColor;
    int nBorder;
    int nBorderColor;
    int nTextType;
    int nAlignType;
    int nStringIndex;
};

struct BinEdit
{
    int nID;
    int nParentID;
    int nTextureSetIndex;
    int nStartX;
    int nStartY;
    int nWidth;
    int nHeight;
    int nFontColor;
    int nBorder;
    int nBorderColor;
    int nTextType;
    int nAlignType;
    int nMaxStringLength;
    int nPassword;
    char szString[128];
};

struct BinProgress
{
    int nID;
    int nParentID;
    int nTextureSetIndex;
    int nCurrent;
    int nMaxValue;
    int nStartX;
    int nStartY;
    int nWidth;
    int nHeight;
    int nProgressColor;
    int nColor;
    int nStyle;
};

static_assert(sizeof(BinPanel) == 40, "UI panel binary layout changed");
static_assert(sizeof(BinButton) == 40, "UI button binary layout changed");
static_assert(sizeof(BinCheckBox) == 32, "UI checkbox binary layout changed");
static_assert(sizeof(BinListBox) == 52, "UI list box binary layout changed");
static_assert(sizeof(BinProgress) == 48, "UI progress binary layout changed");
static_assert(sizeof(BinText) == 52, "UI text binary layout changed");
static_assert(sizeof(BinEdit) == 184, "UI edit binary layout changed");
static_assert(sizeof(Bin3DObj) == 28, "UI 3D object binary layout changed");
static_assert(sizeof(BinGrid) == 40, "UI grid binary layout changed");
