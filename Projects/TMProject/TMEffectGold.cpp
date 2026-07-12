#include "pch.h"
#include "TMEffectGold.h"
#include "TMGlobal.h"

TMEffectGold::TMEffectGold(TMVector3 vecStart, float vecLiveTime)
{
	m_dwStartTime = g_pTimerManager->GetServerTime();
	m_dwLifeTime = static_cast<unsigned int>(vecLiveTime);
	m_vecStartPos = vecStart;
	m_vecEndPos = vecStart;
	m_vecPosition = vecStart;
	m_LiveTime = vecLiveTime;
	m_Hight = 0.0f;
	m_Meshidx = 0;
}

TMEffectGold::~TMEffectGold()
{
}

int TMEffectGold::FrameMove(unsigned int dwServerTime)
{
	return 0;
}

int TMEffectGold::Render()
{
	return 0;
}
