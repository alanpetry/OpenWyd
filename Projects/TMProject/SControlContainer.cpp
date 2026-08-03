#include "pch.h"
#include "TMFieldScene.h"
#include "SControlContainer.h"
#include "OpenWydOptimized.h"
#include "SControl.h"
#include "SGrid.h"
#include "TMGlobal.h"

#if defined(__EMSCRIPTEN__)
namespace {
unsigned int g_wydLastControlEventID = 0;
unsigned int g_wydLastControlEventType = 0;
unsigned int g_wydControlEventCount = 0;
unsigned int g_wydLastMouseProcessedControlID = 0;
unsigned int g_wydLastMouseProcessedFlags = 0;
int g_wydLastMouseProcessedType = -1;
int g_wydLastMouseProcessedX = 0;
int g_wydLastMouseProcessedY = 0;

struct WydVisibleTextControl
{
	unsigned int id;
	int type;
	int align;
	int comma;
	float x;
	float y;
	float width;
	float height;
	float renderX;
	float renderY;
	int textWidth;
	int textHeight;
	unsigned int color;
	char text[256];
};

struct WydControlAuditSample
{
	unsigned int id;
	unsigned int parentId;
	int type;
	int visible;
	int rawVisible;
	int depth;
	float localX;
	float localY;
	float absoluteX;
	float absoluteY;
	float width;
	float height;
};

constexpr unsigned int kWydVisibleTextControlCapacity = 512;
WydVisibleTextControl g_wydVisibleTextControls[kWydVisibleTextControlCapacity]{};
unsigned int g_wydVisibleTextControlCount = 0;
constexpr unsigned int kWydControlAuditCapacity = 8192;
WydControlAuditSample g_wydControlAuditSamples[kWydControlAuditCapacity]{};
SControl* g_wydControlAuditPointers[kWydControlAuditCapacity]{};
unsigned int g_wydControlAuditCount = 0;

SControl* WydFindControl(unsigned int idwControlID)
{
	if (!g_pCurrentScene || !g_pCurrentScene->m_pControlContainer)
		return nullptr;

	return g_pCurrentScene->m_pControlContainer->FindControl(idwControlID);
}

float WydControlAbsX(SControl* pControl)
{
	float x = 0.0f;
	for (SControl* pNode = pControl; pNode != nullptr; pNode = static_cast<SControl*>(pNode->m_pTop))
		x += pNode->m_nPosX;
	return x;
}

float WydControlAbsY(SControl* pControl)
{
	float y = 0.0f;
	for (SControl* pNode = pControl; pNode != nullptr; pNode = static_cast<SControl*>(pNode->m_pTop))
		y += pNode->m_nPosY;
	return y;
}

bool WydControlIsEffectivelyVisible(SControl* pControl)
{
	for (SControl* pNode = pControl; pNode != nullptr; pNode = static_cast<SControl*>(pNode->m_pTop))
	{
		if (!pNode->m_bVisible)
			return false;
	}
	return pControl != nullptr;
}

void WydCollectVisibleTextControls(SControl* pControl)
{
	if (!pControl || g_wydVisibleTextControlCount >= kWydVisibleTextControlCapacity)
		return;

	if (WydControlIsEffectivelyVisible(pControl) &&
		(pControl->m_eCtrlType == CONTROL_TYPE::CTRL_TYPE_TEXT ||
		 pControl->m_eCtrlType == CONTROL_TYPE::CTRL_TYPE_EDITABLETEXT))
	{
		auto pText = static_cast<SText*>(pControl);
		auto& sample = g_wydVisibleTextControls[g_wydVisibleTextControlCount++];
		sample.id = pControl->m_dwControlID;
		sample.type = static_cast<int>(pControl->m_eCtrlType);
		sample.align = static_cast<int>(pText->m_dwAlignType);
		sample.comma = static_cast<int>(pText->m_cComma);
		sample.x = WydControlAbsX(pControl);
		sample.y = WydControlAbsY(pControl);
		sample.width = pControl->m_nWidth;
		sample.height = pControl->m_nHeight;
		sample.renderX = pText->m_GCText.nPosX;
		sample.renderY = pText->m_GCText.nPosY;
		SIZE extent{};
		GetTextExtentPoint32(
			g_pDevice->m_hDC,
			pText->m_GCText.strString,
			strlen(pText->m_GCText.strString),
			&extent);
		sample.textWidth = extent.cx;
		sample.textHeight = extent.cy;
		sample.color = pText->m_GCText.dwColor;
		std::snprintf(sample.text, sizeof(sample.text), "%s", pText->m_GCText.strString);
	}

	for (TreeNode* pChild = pControl->m_pDown; pChild; pChild = pChild->m_pNextLink)
		WydCollectVisibleTextControls(static_cast<SControl*>(pChild));
}

const WydVisibleTextControl* WydVisibleTextControlAt(unsigned int index)
{
	return index < g_wydVisibleTextControlCount ? &g_wydVisibleTextControls[index] : nullptr;
}

void WydRefreshVisibleTextControls()
{
	g_wydVisibleTextControlCount = 0;
	if (!g_pCurrentScene || !g_pCurrentScene->m_pControlContainer)
		return;
	WydCollectVisibleTextControls(g_pCurrentScene->m_pControlContainer->m_pControlRoot);
}

void WydCollectControlAuditSamples(SControl* pControl)
{
	if (!pControl || g_wydControlAuditCount >= kWydControlAuditCapacity)
		return;

	if (pControl->m_dwControlID != 0)
	{
		const unsigned int sampleIndex = g_wydControlAuditCount++;
		auto& sample = g_wydControlAuditSamples[sampleIndex];
		g_wydControlAuditPointers[sampleIndex] = pControl;
		sample.id = pControl->m_dwControlID;
		auto pParent = static_cast<SControl*>(pControl->m_pTop);
		sample.parentId = pParent ? pParent->m_dwControlID : 0;
		sample.type = static_cast<int>(pControl->m_eCtrlType);
		sample.visible = WydControlIsEffectivelyVisible(pControl) ? 1 : 0;
		sample.rawVisible = pControl->m_bVisible ? 1 : 0;
		sample.depth = 0;
		for (SControl* pNode = pControl; pNode && pNode->m_pTop;
			pNode = static_cast<SControl*>(pNode->m_pTop))
		{
			++sample.depth;
		}
		sample.localX = pControl->m_nPosX;
		sample.localY = pControl->m_nPosY;
		sample.absoluteX = WydControlAbsX(pControl);
		sample.absoluteY = WydControlAbsY(pControl);
		sample.width = pControl->m_nWidth;
		sample.height = pControl->m_nHeight;
	}

	for (TreeNode* pChild = pControl->m_pDown; pChild; pChild = pChild->m_pNextLink)
		WydCollectControlAuditSamples(static_cast<SControl*>(pChild));
}

void WydRefreshControlAuditSamples()
{
	g_wydControlAuditCount = 0;
	memset(g_wydControlAuditPointers, 0, sizeof(g_wydControlAuditPointers));
	if (!g_pCurrentScene || !g_pCurrentScene->m_pControlContainer)
		return;
	WydCollectControlAuditSamples(g_pCurrentScene->m_pControlContainer->m_pControlRoot);
}

const WydControlAuditSample* WydControlAuditSampleAt(unsigned int index)
{
	return index < g_wydControlAuditCount ? &g_wydControlAuditSamples[index] : nullptr;
}
} // namespace

extern "C" unsigned int wyd_control_last_event_id() { return g_wydLastControlEventID; }
extern "C" unsigned int wyd_control_last_event_type() { return g_wydLastControlEventType; }
extern "C" unsigned int wyd_control_event_count() { return g_wydControlEventCount; }
extern "C" unsigned int wyd_control_last_mouse_processed_id() { return g_wydLastMouseProcessedControlID; }
extern "C" unsigned int wyd_control_last_mouse_processed_flags() { return g_wydLastMouseProcessedFlags; }
extern "C" int wyd_control_last_mouse_processed_type() { return g_wydLastMouseProcessedType; }
extern "C" int wyd_control_last_mouse_processed_x() { return g_wydLastMouseProcessedX; }
extern "C" int wyd_control_last_mouse_processed_y() { return g_wydLastMouseProcessedY; }
extern "C" int wyd_control_exists(unsigned int idwControlID) { return WydFindControl(idwControlID) ? 1 : 0; }
extern "C" int wyd_control_visible(unsigned int idwControlID)
{
	SControl* pControl = WydFindControl(idwControlID);
	return WydControlIsEffectivelyVisible(pControl) ? 1 : 0;
}
extern "C" int wyd_control_enabled(unsigned int idwControlID)
{
	SControl* pControl = WydFindControl(idwControlID);
	return pControl ? pControl->m_bEnable : 0;
}
extern "C" int wyd_control_select_enabled(unsigned int idwControlID)
{
	SControl* pControl = WydFindControl(idwControlID);
	return pControl ? pControl->m_bSelectEnable : 0;
}
extern "C" int wyd_control_selected(unsigned int idwControlID)
{
	SControl* pControl = WydFindControl(idwControlID);
	if (!pControl || pControl->m_eCtrlType != CONTROL_TYPE::CTRL_TYPE_BUTTON)
		return 0;
	return static_cast<SButton*>(pControl)->m_bSelected == 1 ? 1 : 0;
}
extern "C" int wyd_control_pressed(unsigned int idwControlID)
{
	SControl* pControl = WydFindControl(idwControlID);
	if (!pControl || pControl->m_eCtrlType != CONTROL_TYPE::CTRL_TYPE_BUTTON)
		return 0;
	return static_cast<SButton*>(pControl)->m_bPressed == 1 ? 1 : 0;
}
extern "C" int wyd_control_type(unsigned int idwControlID)
{
	SControl* pControl = WydFindControl(idwControlID);
	return pControl ? static_cast<int>(pControl->m_eCtrlType) : -1;
}
extern "C" float wyd_control_abs_x(unsigned int idwControlID)
{
	SControl* pControl = WydFindControl(idwControlID);
	return pControl ? WydControlAbsX(pControl) : -1.0f;
}
extern "C" float wyd_control_abs_y(unsigned int idwControlID)
{
	SControl* pControl = WydFindControl(idwControlID);
	return pControl ? WydControlAbsY(pControl) : -1.0f;
}
extern "C" float wyd_control_width(unsigned int idwControlID)
{
	SControl* pControl = WydFindControl(idwControlID);
	return pControl ? pControl->m_nWidth : 0.0f;
}
extern "C" float wyd_control_height(unsigned int idwControlID)
{
	SControl* pControl = WydFindControl(idwControlID);
	return pControl ? pControl->m_nHeight : 0.0f;
}
extern "C" unsigned int wyd_control_visible_text_count()
{
	WydRefreshVisibleTextControls();
	return g_wydVisibleTextControlCount;
}
extern "C" unsigned int wyd_control_visible_text_id(unsigned int index)
{
	const auto sample = WydVisibleTextControlAt(index);
	return sample ? sample->id : 0;
}
extern "C" int wyd_control_visible_text_type(unsigned int index)
{
	const auto sample = WydVisibleTextControlAt(index);
	return sample ? sample->type : -1;
}
extern "C" int wyd_control_visible_text_align(unsigned int index)
{
	const auto sample = WydVisibleTextControlAt(index);
	return sample ? sample->align : -1;
}
extern "C" int wyd_control_visible_text_comma(unsigned int index)
{
	const auto sample = WydVisibleTextControlAt(index);
	return sample ? sample->comma : 0;
}
extern "C" float wyd_control_visible_text_x(unsigned int index)
{
	const auto sample = WydVisibleTextControlAt(index);
	return sample ? sample->x : 0.0f;
}
extern "C" float wyd_control_visible_text_y(unsigned int index)
{
	const auto sample = WydVisibleTextControlAt(index);
	return sample ? sample->y : 0.0f;
}
extern "C" float wyd_control_visible_text_width(unsigned int index)
{
	const auto sample = WydVisibleTextControlAt(index);
	return sample ? sample->width : 0.0f;
}
extern "C" float wyd_control_visible_text_height(unsigned int index)
{
	const auto sample = WydVisibleTextControlAt(index);
	return sample ? sample->height : 0.0f;
}
extern "C" float wyd_control_visible_text_render_x(unsigned int index)
{
	const auto sample = WydVisibleTextControlAt(index);
	return sample ? sample->renderX : 0.0f;
}
extern "C" float wyd_control_visible_text_render_y(unsigned int index)
{
	const auto sample = WydVisibleTextControlAt(index);
	return sample ? sample->renderY : 0.0f;
}
extern "C" int wyd_control_visible_text_extent_width(unsigned int index)
{
	const auto sample = WydVisibleTextControlAt(index);
	return sample ? sample->textWidth : 0;
}
extern "C" int wyd_control_visible_text_extent_height(unsigned int index)
{
	const auto sample = WydVisibleTextControlAt(index);
	return sample ? sample->textHeight : 0;
}
extern "C" unsigned int wyd_control_visible_text_color(unsigned int index)
{
	const auto sample = WydVisibleTextControlAt(index);
	return sample ? sample->color : 0;
}
extern "C" const char* wyd_control_visible_text_value(unsigned int index)
{
	const auto sample = WydVisibleTextControlAt(index);
	return sample ? sample->text : "";
}
extern "C" unsigned int wyd_control_audit_count()
{
	WydRefreshControlAuditSamples();
	return g_wydControlAuditCount;
}
extern "C" unsigned int wyd_control_audit_id(unsigned int index)
{
	const auto sample = WydControlAuditSampleAt(index);
	return sample ? sample->id : 0;
}
extern "C" unsigned int wyd_control_audit_parent_id(unsigned int index)
{
	const auto sample = WydControlAuditSampleAt(index);
	return sample ? sample->parentId : 0;
}
extern "C" int wyd_control_audit_type(unsigned int index)
{
	const auto sample = WydControlAuditSampleAt(index);
	return sample ? sample->type : -1;
}
extern "C" int wyd_control_audit_visible(unsigned int index)
{
	const auto sample = WydControlAuditSampleAt(index);
	return sample ? sample->visible : 0;
}
extern "C" int wyd_control_audit_raw_visible(unsigned int index)
{
	const auto sample = WydControlAuditSampleAt(index);
	return sample ? sample->rawVisible : 0;
}
extern "C" int wyd_control_audit_depth(unsigned int index)
{
	const auto sample = WydControlAuditSampleAt(index);
	return sample ? sample->depth : -1;
}
extern "C" int wyd_control_audit_set_raw_visible(unsigned int index, int visible)
{
	if (index >= g_wydControlAuditCount || !g_wydControlAuditPointers[index])
		return 0;
	// This is deliberately a visual-audit primitive.  Calling the virtual
	// SetVisible hook can trigger gameplay/UI side effects; changing only the
	// raw flag lets the test render an official control tree without inventing
	// state transitions or mutating scene data.
	g_wydControlAuditPointers[index]->m_bVisible = visible ? 1 : 0;
	return 1;
}
extern "C" int wyd_control_audit_reveal_with_ancestors(unsigned int index)
{
	if (index >= g_wydControlAuditCount || !g_wydControlAuditPointers[index])
		return 0;
	for (SControl* pNode = g_wydControlAuditPointers[index]; pNode;
		pNode = static_cast<SControl*>(pNode->m_pTop))
	{
		pNode->m_bVisible = 1;
	}
	return 1;
}
extern "C" int wyd_control_audit_prepare_runtime_panel(unsigned int idwControlID)
{
	if (!g_pCurrentScene || g_pCurrentScene->m_eSceneType != ESCENE_TYPE::ESCENE_FIELD)
		return 0;

	// The minimap is intentionally stored at an inactive RC position and is
	// resized/repositioned only when the official toggle runs.  Auditing its raw
	// hidden tree produces a false off-screen result, so exercise that same
	// production path before capturing it.
	if (idwControlID == 289)
	{
		auto* pField = static_cast<TMFieldScene*>(g_pCurrentScene);
		if (!pField->m_pMiniMapPanel)
			return 0;
		if (!pField->m_pMiniMapPanel->m_bVisible)
			pField->SetVisibleMiniMap();
		return pField->m_pMiniMapPanel->m_bVisible ? 1 : 0;
	}

	return 0;
}
extern "C" float wyd_control_audit_local_x(unsigned int index)
{
	const auto sample = WydControlAuditSampleAt(index);
	return sample ? sample->localX : 0.0f;
}
extern "C" float wyd_control_audit_local_y(unsigned int index)
{
	const auto sample = WydControlAuditSampleAt(index);
	return sample ? sample->localY : 0.0f;
}
extern "C" float wyd_control_audit_abs_x(unsigned int index)
{
	const auto sample = WydControlAuditSampleAt(index);
	return sample ? sample->absoluteX : 0.0f;
}
extern "C" float wyd_control_audit_abs_y(unsigned int index)
{
	const auto sample = WydControlAuditSampleAt(index);
	return sample ? sample->absoluteY : 0.0f;
}
extern "C" float wyd_control_audit_width(unsigned int index)
{
	const auto sample = WydControlAuditSampleAt(index);
	return sample ? sample->width : 0.0f;
}
extern "C" float wyd_control_audit_height(unsigned int index)
{
	const auto sample = WydControlAuditSampleAt(index);
	return sample ? sample->height : 0.0f;
}
extern "C" int wyd_control_grid_rows(unsigned int idwControlID)
{
	SControl* pControl = WydFindControl(idwControlID);
	if (!pControl || pControl->m_eCtrlType != CONTROL_TYPE::CTRL_TYPE_GRID)
		return 0;
	return static_cast<SGridControl*>(pControl)->m_nRowGridCount;
}
extern "C" int wyd_control_grid_columns(unsigned int idwControlID)
{
	SControl* pControl = WydFindControl(idwControlID);
	if (!pControl || pControl->m_eCtrlType != CONTROL_TYPE::CTRL_TYPE_GRID)
		return 0;
	return static_cast<SGridControl*>(pControl)->m_nColumnGridCount;
}
extern "C" int wyd_control_grid_item_count(unsigned int idwControlID)
{
	SControl* pControl = WydFindControl(idwControlID);
	if (!pControl || pControl->m_eCtrlType != CONTROL_TYPE::CTRL_TYPE_GRID)
		return 0;
	return static_cast<SGridControl*>(pControl)->m_nNumItem;
}
extern "C" float wyd_control_grid_item_width(unsigned int idwControlID, unsigned int index)
{
	SControl* pControl = WydFindControl(idwControlID);
	if (!pControl || pControl->m_eCtrlType != CONTROL_TYPE::CTRL_TYPE_GRID)
		return 0.0f;
	auto pGrid = static_cast<SGridControl*>(pControl);
	return index < static_cast<unsigned int>(pGrid->m_nNumItem) && pGrid->m_pItemList[index]
		? pGrid->m_pItemList[index]->m_nWidth : 0.0f;
}
extern "C" float wyd_control_grid_item_height(unsigned int idwControlID, unsigned int index)
{
	SControl* pControl = WydFindControl(idwControlID);
	if (!pControl || pControl->m_eCtrlType != CONTROL_TYPE::CTRL_TYPE_GRID)
		return 0.0f;
	auto pGrid = static_cast<SGridControl*>(pControl);
	return index < static_cast<unsigned int>(pGrid->m_nNumItem) && pGrid->m_pItemList[index]
		? pGrid->m_pItemList[index]->m_nHeight : 0.0f;
}
extern "C" int wyd_control_grid_item_cell_x(unsigned int idwControlID, unsigned int index)
{
	SControl* pControl = WydFindControl(idwControlID);
	if (!pControl || pControl->m_eCtrlType != CONTROL_TYPE::CTRL_TYPE_GRID)
		return -1;
	auto* pGrid = static_cast<SGridControl*>(pControl);
	return index < static_cast<unsigned int>(pGrid->m_nNumItem) && pGrid->m_pItemList[index]
		? pGrid->m_pItemList[index]->m_nCellIndexX : -1;
}
extern "C" int wyd_control_grid_item_sindex(unsigned int idwControlID, unsigned int index)
{
	SControl* pControl = WydFindControl(idwControlID);
	if (!pControl || pControl->m_eCtrlType != CONTROL_TYPE::CTRL_TYPE_GRID)
		return -1;
	auto* pGrid = static_cast<SGridControl*>(pControl);
	auto* pGridItem = index < static_cast<unsigned int>(pGrid->m_nNumItem)
		? pGrid->m_pItemList[index] : nullptr;
	return pGridItem && pGridItem->m_pItem ? pGridItem->m_pItem->sIndex : -1;
}
extern "C" int wyd_control_selected_short_skill()
{
	return g_pObjectManager
		? static_cast<unsigned char>(g_pObjectManager->m_cSelectShortSkill) : -1;
}
extern "C" int wyd_control_assigned_short_skill(unsigned int index)
{
	return g_pObjectManager && index < 20
		? static_cast<unsigned char>(g_pObjectManager->m_cShortSkill[index]) : -1;
}
extern "C" int wyd_control_audit_click_skill_cell(unsigned int idwControlID, unsigned int cell)
{
	SControl* pControl = WydFindControl(idwControlID);
	if (!pControl || pControl->m_eCtrlType != CONTROL_TYPE::CTRL_TYPE_GRID || !g_pCursor)
		return -1;
	auto* pGrid = static_cast<SGridControl*>(pControl);
	if (pGrid->m_eGridType != TMEGRIDTYPE::GRID_SKILLB ||
		cell >= static_cast<unsigned int>(pGrid->m_nColumnGridCount))
		return -1;

	const ECursorStyle previousStyle = g_pCursor->GetStyle();
	g_pCursor->SetStyle(ECursorStyle::TMC_CURSOR_HAND);
	const int x = static_cast<int>(std::floor(
		pGrid->m_nPosX + ((static_cast<float>(cell) + 0.5f) * pGrid->m_nWidth) /
		static_cast<float>(pGrid->m_nColumnGridCount)));
	const int y = static_cast<int>(std::floor(pGrid->m_nPosY + pGrid->m_nHeight * 0.5f));
	const int processed = pGrid->OnMouseEvent(514, 0, x, y);
	g_pCursor->SetStyle(previousStyle);
	return processed ? wyd_control_selected_short_skill() : -1;
}
extern "C" int wyd_control_audit_populate_skill_belt()
{
	if (!g_pCurrentScene || g_pCurrentScene->m_eSceneType != ESCENE_TYPE::ESCENE_FIELD)
		return 0;

	auto pField = static_cast<TMFieldScene*>(g_pCurrentScene);
	if (!pField->m_pGridSkillBelt2 || !pField->m_pGridSkillBelt3 || !g_pObjectManager)
		return 0;

	for (int index = 0; index < 20; ++index)
		g_pObjectManager->m_cShortSkill[index] = static_cast<char>(index);
	g_pObjectManager->m_cSelectShortSkill = 0;
	pField->UpdateSkillBelt();
	return pField->m_pGridSkillBelt2->m_nNumItem + pField->m_pGridSkillBelt3->m_nNumItem;
}
#endif

SControlContainer::SControlContainer(TMScene* pScene) 
	: TreeNode(0)
	, m_pScene(pScene)
{
	m_pFocusControl = nullptr;
	m_pPickedControl = nullptr;

	m_pCursor = new SCursor(0, g_pDevice->m_dwScreenWidth / 2.0f, g_pDevice->m_dwScreenHeight / 2.0f, 32.0f, 32.0f);
	m_pControlRoot = new SControl(0.0f, 0.0f, 0.0f, 0.0f);

	m_bCleanUp = 0;
	m_bInvisibleUI = 0;

	memset(m_pDrawControl, 0, sizeof m_pDrawControl);
	memset(m_pModalControl, 0, sizeof m_pModalControl);
}

SControlContainer::~SControlContainer()
{
	SAFE_DELETE(m_pControlRoot);
	SAFE_DELETE(m_pCursor);
}

int SControlContainer::OnMouseEvent(unsigned int dwFlags, unsigned int wParam, int nX, int nY)
{
	if (m_pCursor->m_bVisible)
		m_pCursor->OnMouseEvent(dwFlags, wParam, nX, nY);

	int ParentPosX{ 0 };
	int ParentPosY{ 0 };
	int bProcessed{ 0 };

	auto pCurrentControl = m_pControlRoot;
	auto pRootControl = m_pControlRoot;
	for (int i = 0; i < 8; ++i)
	{
		SControl* tmp = m_pModalControl[i];
		if (tmp != nullptr && tmp->m_bVisible == 1 && tmp->m_bModal == 1)
		{
			pCurrentControl = tmp;
			pRootControl = tmp;

			break;
		}
	}

	if (pCurrentControl == nullptr)
		return 1;

	int b{ 0 };
	int before{ 0 };
	do
	{
		if (!pCurrentControl->m_cDeleted && pCurrentControl->m_bVisible)
		{
			before = pCurrentControl->m_bFocused;

			int ret = pCurrentControl->OnMouseEvent(dwFlags, wParam, nX - ParentPosX, nY - ParentPosY);
#if defined(__EMSCRIPTEN__)
			if (ret == 1)
			{
				g_wydLastMouseProcessedControlID = pCurrentControl->m_dwControlID;
				g_wydLastMouseProcessedFlags = dwFlags;
				g_wydLastMouseProcessedType = static_cast<int>(pCurrentControl->m_eCtrlType);
				g_wydLastMouseProcessedX = nX - ParentPosX;
				g_wydLastMouseProcessedY = nY - ParentPosY;
			}
#endif
			if (pCurrentControl->m_bFocused && !before && ret == 1 && pCurrentControl->m_eCtrlType == CONTROL_TYPE::CTRL_TYPE_EDITABLETEXT)
				SetFocusedControl(pCurrentControl);

			if (ret == 1)
				bProcessed = 1;

			if (pCurrentControl->m_pDown)
			{
				ParentPosX += static_cast<int>(pCurrentControl->m_nPosX);
				ParentPosY += static_cast<int>(pCurrentControl->m_nPosY);

				pCurrentControl = static_cast<SControl*>(pCurrentControl->m_pDown);
				continue;
			}
		}

		do
		{
			if (pCurrentControl->m_pNextLink != nullptr)
			{
				pCurrentControl = static_cast<SControl*>(pCurrentControl->m_pNextLink);
				break;
			}

			pCurrentControl = static_cast<SControl*>(pCurrentControl->m_pTop);

			if (pCurrentControl == nullptr)
				break;

			ParentPosX -= static_cast<int>(pCurrentControl->m_nPosX);
			ParentPosY -= static_cast<int>(pCurrentControl->m_nPosY);
			++b;
		} while (pCurrentControl != pRootControl);
	} while (pCurrentControl != pRootControl && pCurrentControl != nullptr);

	if (!bProcessed)
	{
		auto pPanel = g_pCurrentScene->m_pDescPanel;
		if (pPanel)
			pPanel->SetVisible(0);
	}

	return bProcessed;
}

int SControlContainer::OnKeyDownEvent(unsigned int iKeyCode)
{
	auto pCurrentControl = m_pControlRoot;
	auto pRootControl = m_pControlRoot;

	if (pCurrentControl == nullptr)
		return 0;

	do
	{
		if (!pCurrentControl->m_cDeleted && pCurrentControl->m_bVisible == 1)
		{
			if (pCurrentControl->OnKeyDownEvent(iKeyCode))
				return 1;

			if (pCurrentControl->m_pDown)
			{
				pCurrentControl = static_cast<SControl*>(pCurrentControl->m_pDown);

				continue;
			}
		}

		do
		{
			if (pCurrentControl->m_pNextLink != nullptr)
			{
				pCurrentControl = static_cast<SControl*>(pCurrentControl->m_pNextLink);
				break;
			}

			pCurrentControl = static_cast<SControl*>(pCurrentControl->m_pTop);
		} while (pCurrentControl != pRootControl && pCurrentControl != nullptr);
	} while (pCurrentControl != pRootControl && pCurrentControl != nullptr);

	return 0;
}

int SControlContainer::OnKeyUpEvent(unsigned int iKeyCode)
{
	auto pCurrentControl = m_pControlRoot;
	auto pRootControl = m_pControlRoot;

	if (pCurrentControl == nullptr)
		return 0;

	do
	{
		if (pCurrentControl->m_cDeleted && pCurrentControl->m_bVisible == 1)
		{
			if (pCurrentControl->OnKeyUpEvent(iKeyCode))
				return 1;

			if (pCurrentControl->m_pDown)
			{
				pCurrentControl = static_cast<SControl*>(m_pDown);

				continue;
			}
		}

		do
		{
			if (pCurrentControl->m_pNextLink != nullptr)
			{
				pCurrentControl = static_cast<SControl*>(pCurrentControl->m_pNextLink);
				break;
			}

			pCurrentControl = static_cast<SControl*>(pCurrentControl->m_pTop);
		} while (pCurrentControl != pRootControl && pCurrentControl != nullptr);
	} while (pCurrentControl != pRootControl && pCurrentControl != nullptr);

	return 0;
}

int SControlContainer::OnCharEvent(char iCharCode, int lParam)
{
	return m_pFocusControl == nullptr ? 0 : m_pFocusControl->OnCharEvent(iCharCode, lParam);
}

int SControlContainer::OnChangeIME()
{
	return m_pFocusControl == nullptr ? 0 : m_pFocusControl->OnChangeIME();
}

int SControlContainer::OnIMEEvent(char* ipComposeString)
{
	return m_pFocusControl == nullptr ? 0 : m_pFocusControl->OnIMEEvent(ipComposeString);
}

void SControlContainer::SetFocusedControl(SControl* pControl)
{
	if (g_nKeyType != 1 || pControl && pControl->m_eCtrlType == CONTROL_TYPE::CTRL_TYPE_EDITABLETEXT)
	{
		if (m_pFocusControl)
			m_pFocusControl->SetFocused(0);

		m_pFocusControl = pControl;

		if(m_pFocusControl)
			m_pFocusControl->SetFocused(1);

		TMScene* pScene = g_pCurrentScene;
		if (pScene)
		{
			if (m_pFocusControl != nullptr && m_pFocusControl->m_eCtrlType == CONTROL_TYPE::CTRL_TYPE_EDITABLETEXT)
			{
				pScene->m_pAlphaNative->SetVisible(1);
				g_pEventTranslator->UpdateCompositionPos();

				SPanel* panel = static_cast<SPanel*>(pScene->m_pControlContainer->FindControl(P_CHAT));
				if (panel && panel->m_bVisible && pScene->m_eSceneType == ESCENE_TYPE::ESCENE_FIELD)
					static_cast<TMFieldScene*>(pScene)->m_pChatSelectPanel->SetVisible(1);
			}
			else
			{
				pScene->m_pAlphaNative->SetVisible(0);

				if (pScene->m_eSceneType == ESCENE_TYPE::ESCENE_FIELD)
				{
					static_cast<TMFieldScene*>(pScene)->m_pChatSelectPanel->SetVisible(0);
					static_cast<TMFieldScene*>(pScene)->m_pChatListPanel->SetVisible(0);
				}

				pScene->m_pTextIMEDesc->SetVisible(0);
			}

			if (m_pFocusControl && m_pFocusControl->IsIMENative())
			{
				if (g_pEventTranslator)
					g_pEventTranslator->SetIMENative();
			}
			else if (g_pEventTranslator)
				g_pEventTranslator->SetIMEAlphaNumeric();
		}
	}
}

int SControlContainer::OnControlEvent(DWORD idwControlID, DWORD idwEvent)
{
#if defined(__EMSCRIPTEN__)
	g_wydLastControlEventID = idwControlID;
	g_wydLastControlEventType = idwEvent;
	++g_wydControlEventCount;
#endif
	return m_pScene ? m_pScene->OnControlEvent(idwControlID, idwEvent) : 0;
}

void SControlContainer::AddItem(SControl* pControl)
{
	OpenWydOptimizedConfigureRootControl(pControl);
	m_pControlRoot->AddChild(pControl);
}

int SControlContainer::FrameMove(unsigned int dwServerTime)
{
	TMVector2 vParentPos{};
	auto pCurrentControl = m_pControlRoot;
	auto pRootControl = m_pControlRoot;
	int vControlLayer = 0;
	if (pCurrentControl == nullptr)
		return 1;

	if (m_bInvisibleUI == 1)
		return 1;

	do
	{
		if (!pCurrentControl->m_cDeleted)
		{
			if (pCurrentControl->m_bVisible == 1)
			{
				pCurrentControl->FrameMove2(m_pDrawControl, vParentPos, vControlLayer, 0);

				if (pCurrentControl->m_pDown)
				{
					vParentPos.x += pCurrentControl->m_nPosX;
					vParentPos.y += pCurrentControl->m_nPosY;
					++vControlLayer;

					pCurrentControl = static_cast<SControl*>(pCurrentControl->m_pDown);
					continue;
				}
			}
		}
		else
			m_bCleanUp = 1;

		do
		{
			if (pCurrentControl->m_pNextLink != nullptr)
			{
				pCurrentControl = static_cast<SControl*>(pCurrentControl->m_pNextLink);
				break;
			}

			pCurrentControl = static_cast<SControl*>(pCurrentControl->m_pTop);
			vParentPos.x -= pCurrentControl->m_nPosX;
			vParentPos.y -= pCurrentControl->m_nPosY;
		} while (pCurrentControl != pRootControl && pCurrentControl != nullptr);
	} while (pCurrentControl != pRootControl && pCurrentControl != nullptr);

	if (m_pCursor->m_bVisible)
		m_pCursor->FrameMove2(m_pDrawControl, vParentPos, 29, 0);

	return 1;
}

SControl* SControlContainer::FindControl(unsigned int dwID)
{
	SControl* pCurrentControl = m_pControlRoot;
	SControl* pRootControl = m_pControlRoot;

	if (!pCurrentControl)
		return nullptr;

	do
	{
		if (!pCurrentControl->m_cDeleted)
		{
			if (pCurrentControl->GetControlID() == dwID)
				return pCurrentControl;

			if (pCurrentControl->m_pDown)
			{
				pCurrentControl = static_cast<SControl*>(pCurrentControl->m_pDown);
				continue;
			}
		}

		do
		{
			if (pCurrentControl->m_pNextLink != nullptr)
			{
				pCurrentControl = static_cast<SControl*>(pCurrentControl->m_pNextLink);
				break;
			}

			pCurrentControl = static_cast<SControl*>(pCurrentControl->m_pTop);
		} while (pCurrentControl != pRootControl && pCurrentControl != nullptr);
	} while (pCurrentControl != pRootControl && pCurrentControl != nullptr);

	return nullptr;
}

void SControlContainer::GenerateText(const char* pFileName)
{
	// Skip that function for now... just create a text file with content on screen
	/*FILE* fp = nullptr;
	fopen_s(&fp, pFileName, "wt");

	if (fp)
	{
		SControl* pCurrentControl = m_pControlRoot;
		SControl* pRootControl = m_pControlRoot;

		if (pCurrentControl)
		{
			do 
			{
				if (!pCurrentControl->m_cDeleted)
				{
					if (pCurrentControl->m_eCtrlType == CONTROL_TYPE::CTRL_TYPE_TEXT)
					{
						for (int i = 0 < MAX_RESOURCE_LIST; ++i)
						{
							if (g_pObjectManager->m_ResourceList[i].nNumber == pCurrentControl->m_dwControlID)
							{
								const char* text = TMFont2 pCurrentControl->
							}
						}
					}
				}


			} while (pCurrentControl != pRootControl && pCurrentControl != nullptr);
		}

		fclose(fp);
	}*/
}
