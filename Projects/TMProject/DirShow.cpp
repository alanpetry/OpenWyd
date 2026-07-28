#include "pch.h"
#include "DirShow.h"
#include "TMGlobal.h"

#if defined(__EMSCRIPTEN__)
extern "C" int wyd_audio_play_music_file(const char* path, int volume);
extern "C" int wyd_audio_stop_music();
extern "C" int wyd_audio_set_music_volume(int volume);
extern "C" int wyd_audio_get_music_volume();
#endif

int DS_SOUND_MANAGER::m_nMusicIndex = -1;
int DS_SOUND_MANAGER::m_nCastleIndex = -1;
char DS_SOUND_MANAGER::m_szMusicPathOrigin[15][256] = {
	"music\\login.mp3",
	"music\\town01.mp3",
	"music\\field01.mp3",
	"music\\town02.mp3",
	"music\\field02.mp3",
	"music\\dungeon01.mp3",
	"music\\kingdom.mp3",
	"music\\dungeon02.mp3",
	"music\\town03.mp3",
	"music\\field03.mp3",
	"music\\CastleWar.mp3",
	"music\\kepra.mp3",
	"music\\khepraBoss.mp3",
	"",
	""
};

char DS_SOUND_MANAGER::m_szMusicPath[15][256] = {
	"music\\login.mp3",
	"music\\town01.mp3",
	"music\\field01.mp3",
	"music\\town02.mp3",
	"music\\field02.mp3",
	"music\\dungeon01.mp3",
	"music\\kingdom.mp3",
	"music\\dungeon02.mp3",
	"music\\town03.mp3",
	"music\\field03.mp3",
	"music\\CastleWar.mp3",
	"music\\kepra.mp3",
	"music\\khepraBoss.mp3",
	"",
	""
};

void ConvertBGM(const char* szFileName)
{
	static char byte_801BB0[172] =
	{
	  '\xB1', '\xDD', '\xB0', '\xAD', '\xBB', '\xEA', '\xC3', '\xA3', '\xBE', '\xC6', '\xB0', '\xA1', '\xC0', '\xDA', '\xC0', '\xCF', '\xB8', '\xB8', '\xC0', '\xCC', '\xC3', '\xB5', '\xBA',
	  '\xC0', '\xBA', '\xBC', '\xBC', '\xF6', '\xB7', '\xCF', '\xBE', '\xC6', '\xB8', '\xA7', '\xB4', '\xE4', '\xB0', '\xED', '\xBD', '\xC5', '\xBA', '\xF1', '\xC7', '\xCF', '\xB1', '\xB8',
	  '\xB3', '\xAA', '\xBF', '\xEC', '\xB8', '\xAE', '\xB3', '\xAA', '\xB6', '\xF3', '\xC1', '\xC1', '\xC0', '\xBA', '\xB3', '\xAA', '\xB6', '\xF3', '\xBB', '\xF5', '\xB3', '\xAA', '\xB6',
	  '\xF3', '\xC0', '\xC7', '\xBE', '\xEE', '\xB8', '\xB0', '\xC0', '\xCC', '\xB4', '\xC2', '\xC0', '\xCF', '\xC2', '\xEF', '\xC0', '\xCF', '\xBE', '\xEE', '\xB3', '\xB3', '\xB4', '\xCF',
	  '\xB4', '\xD9', '\xC0', '\xE1', '\xB2', '\xD9', '\xB7', '\xAF', '\xB1', '\xE2', '\xBE', '\xF8', '\xB4', '\xC2', '\xB3', '\xAA', '\xB6', '\xF3', '\xBF', '\xEC', '\xB8', '\xAE', '\xB3',
	  '\xAA', '\xB6', '\xF3', '\xC1', '\xC1', '\xC0', '\xBA', '\xB3', '\xAA', '\xB6', '\xF3', '\xB9', '\xAB', '\xB1', '\xC3', '\xC8', '\xAD', '\xB9', '\xAB', '\xB1', '\xC3', '\xC8', '\xAD',
	  '\xBF', '\xEC', '\xB8', '\xAE', '\xB3', '\xAA', '\xB6', '\xF3', '\xB2', '\xC9', '\xBB', '\xEF', '\xC3', '\xB5', '\xB8', '\xAE', '\xB0', '\xAD', '\xBB', '\xEA', '\xBF', '\xA1', '\xBF',
	  '\xEC', '\xB8', '\xAE', '\xB3', '\xAA', '\xB6', '\xF3', '\xB2', '\xC9', '\0'
	};

	if (!szFileName || !szFileName[0])
		return;

	char szTemp[MAX_PATH]{};
	sprintf_s(szTemp, "%s", szFileName);

	char* pExtension = strrchr(szTemp, '.');
	if (pExtension)
		sprintf_s(pExtension, MAX_PATH - static_cast<size_t>(pExtension - szTemp), ".bon");
	else
	{
		const size_t nLength = strlen(szTemp);
		sprintf_s(szTemp + nLength, MAX_PATH - nLength, ".bon");
	}

	auto handle = _open(szTemp, 0x8000, 0);
	if (handle != -1)
	{
		auto sz = _filelength(handle);
		if (sz <= 0)
		{
			_close(handle);
			return;
		}

		auto pBuffer = (char*)malloc(sz);
		if (!pBuffer)
		{
			_close(handle);
			return;
		}

		_read(handle, pBuffer, sz);
		_close(handle);

		int nLen2 = strlen(byte_801BB0);
		for (int i = 0; i < sz; ++i)
			pBuffer[i] -= byte_801BB0[i % nLen2];

		auto fp = fopen(szFileName, "wb");
		if (fp)
		{
			fwrite(pBuffer, sz, 1, fp);
			fclose(fp);
		}

		free(pBuffer);
	}
}

DS_SOUND_CHANNEL::DS_SOUND_CHANNEL()
{
	basic_audio = nullptr;
	media_seeking = nullptr;
	media_control = nullptr;
	graph_builder = nullptr;
	media_event = nullptr;
	init_flag = false;

	CoInitialize(0);

	if (FAILED(CoCreateInstance(CLSID_FilterGraph, nullptr, CLSCTX_INPROC_SERVER | CLSCTX_INPROC_HANDLER, IID_IGraphBuilder, (LPVOID*)&graph_builder)))
		init_flag = false;
	else if (FAILED(graph_builder->QueryInterface(IID_IMediaControl, (void**)&media_control)))
		init_flag = false;
	else if (FAILED(graph_builder->QueryInterface(IID_IMediaSeeking, (void**)&media_seeking)))
		init_flag = false;
	else if (FAILED(graph_builder->QueryInterface(IID_IBasicAudio, (void**)&basic_audio)))
		init_flag = false;
	else if (FAILED(graph_builder->QueryInterface(IID_IMediaEventEx, (void**)&media_event)))
		init_flag = false;
	else
	{
		init_flag = SUCCEEDED(media_event->SetNotifyWindow((OAHWND)g_pApp->GetSafeHwnd(), 1125, 0));
	}
}

DS_SOUND_CHANNEL::~DS_SOUND_CHANNEL()
{
	CleanGraph();

	SAFE_RELEASE(media_event);
	SAFE_RELEASE(basic_audio);
	SAFE_RELEASE(media_seeking);
	SAFE_RELEASE(media_control);
	SAFE_RELEASE(graph_builder);

	CoUninitialize();
}

void DS_SOUND_CHANNEL::InitClass()
{
	CleanGraph();

	SAFE_RELEASE(media_event);
	SAFE_RELEASE(basic_audio);
	SAFE_RELEASE(media_seeking);
	SAFE_RELEASE(media_control);
	SAFE_RELEASE(graph_builder);

	CoUninitialize();
}

char DS_SOUND_CHANNEL::CleanGraph()
{
	if (!init_flag)
		return 0;

	if (media_control)
		media_control->Stop();

	IEnumFilters* pFilterEnum;
	if (FAILED(graph_builder->EnumFilters(&pFilterEnum)))
		return 0;

	int iFiltCount = 0;
	int iPos = 0;

	while (SUCCEEDED(pFilterEnum->Skip(1u)))
		++iFiltCount;

	IBaseFilter** ppFilters = (IBaseFilter**)_malloca(sizeof(IBaseFilter*) * iFiltCount);

	HRESULT nextFilterHR = S_OK;
	IBaseFilter** nextFilter = nullptr;
	pFilterEnum->Reset();

	while (pFilterEnum->Next(1, &(ppFilters[iPos++]), NULL) == S_OK)
		;

	SAFE_RELEASE(pFilterEnum);

	for (int iPos = 0; iPos < iFiltCount; ++iPos)
	{
		graph_builder->RemoveFilter(ppFilters[iPos]);

		while (ppFilters[iPos]->Release())
			;
	}

	return 1;
}

bool DS_SOUND_CHANNEL::HasFilter(IBaseFilter* filter)
{
	// not used
	return false;
}

FILTER_STATE DS_SOUND_CHANNEL::GetState()
{
	// not used
	return FILTER_STATE();
}

void DS_SOUND_CHANNEL::OnEvent()
{
	long lParam1;
	long lParam2;
	long lEventCode;

	if (!media_event->GetEvent(&lEventCode, &lParam1, &lParam2, 0) && lEventCode == 1)
	{
		Stop();
		SetPosition(0ll);
		Run();
	}
}

IGraphBuilder* DS_SOUND_CHANNEL::GetGraphBuilder()
{
	return graph_builder;
}

HRESULT DS_SOUND_CHANNEL::GetVolume(long* vol)
{
	return basic_audio ? basic_audio->get_Volume(vol) : E_FAIL;
}

HRESULT DS_SOUND_CHANNEL::SetVolume(long vol)
{
	return basic_audio ? basic_audio->put_Volume(vol) : E_FAIL;
}

HRESULT DS_SOUND_CHANNEL::SetBalance(long bal)
{
	return basic_audio ? basic_audio->put_Balance(bal) : E_FAIL;
}

HRESULT DS_SOUND_CHANNEL::Run()
{
	return media_control ? media_control->Run() : E_FAIL;
}

HRESULT DS_SOUND_CHANNEL::Stop()
{
	return media_control ? media_control->Stop() : E_FAIL;
}

HRESULT DS_SOUND_CHANNEL::Pause()
{
	return media_control ? media_control->Pause() : E_FAIL;
}

HRESULT DS_SOUND_CHANNEL::SetPosition(long long pos)
{
	return media_seeking ? media_seeking->SetPositions(&pos, 1u, &pos, 0) : E_FAIL;
}

DS_SOUND_MANAGER::DS_SOUND_MANAGER(int channel_num, int lBGMVolume)
{
	m_hwndASFPlayer = NULL;
	this->channel_num = channel_num;
	cur_channel = 1;
	m_lBGMVolume = lBGMVolume;

#if defined(__EMSCRIPTEN__)
	channels = nullptr;
	init_flag = channel_num >= 1;
	return;
#else
//	int* block = new int[24 * channel_num | -((24 * channel_num >> 32) != 0) + 4];
	channels = new DS_SOUND_CHANNEL[channel_num];

	if (channels && channel_num >= 1)
	{
		init_flag = 1;
	}
	else
		init_flag = 0;
#endif
}

DS_SOUND_MANAGER::~DS_SOUND_MANAGER()
{
#if defined(__EMSCRIPTEN__)
	wyd_audio_stop_music();
#endif
	if (channels)
		delete[] channels;

	if (m_szMusicPathOrigin[13][0])
		DeleteFileA(m_szMusicPathOrigin[13]);
	if (m_szMusicPathOrigin[14][0])
		DeleteFileA(m_szMusicPathOrigin[14]);
}

void DS_SOUND_MANAGER::InitClass(int channel_num)
{
	// not used
	if (channels)
	{
		delete[] channels;

		channels = nullptr;
	}
}

int DS_SOUND_MANAGER::PlaySoundA(const char* path, const bool BGM_flag)
{
	auto patha = path;
	if (!init_flag)
		return -1;

#if defined(__EMSCRIPTEN__)
	if (!patha || !BGM_flag)
		return -1;
	return wyd_audio_play_music_file(patha, m_lBGMVolume) ? 0 : -1;
#else
	int channel = 0;
	if (BGM_flag == 1)
		channel = 0;
	else
	{
		if (channel_num < 2)
			return -1;

		channel = cur_channel;
	}

	auto graph_builder = channels[channel].GetGraphBuilder();
	if (!graph_builder)
		return -1;

	struct _stat64i32 temp;
	if (_stat64i32(patha, &temp))
		return -1;

	wchar_t wFileName[260];
	MultiByteToWideChar(0, 0, patha, -1, wFileName, sizeof(wFileName) / 2);

	if (channel && channels[channel].Stop())
		return -1;

	if (!channels[channel].CleanGraph())
		return -1;

	IBaseFilter* temp_filter;
	if(FAILED(graph_builder->AddSourceFilter(wFileName, wFileName, &temp_filter)))
		return -1;

	IPin* pPin;
	if (FAILED(temp_filter->FindPin(L"Output", &pPin)))
		return -1;

	if (graph_builder->Render(pPin))
	{
		SAFE_RELEASE(pPin);
		return -1;
	}

	SAFE_RELEASE(pPin);
	SAFE_RELEASE(temp_filter);

	channels[channel].SetPosition(0ll);
	channels[channel].Run();

	if (BGM_flag == 1)
		return 0;
	
	auto play_channel = cur_channel;
	if (cur_channel + 1 == channel_num)
		cur_channel = 1;
	else
		++cur_channel;

	return play_channel;
#endif
}

int DS_SOUND_MANAGER::PlayBGM(const char* path)
{
	return PlaySoundA(path, 1);
}

void DS_SOUND_MANAGER::PlayMusic(int nIndex)
{
	m_nMusicIndex = nIndex;

	if (nIndex >= 0 && nIndex < 15)
	{
		StopBGM();

		if (nIndex == 13)
			ConvertBGM(m_szMusicPathOrigin[13]);
		else if (nIndex == 14)
			ConvertBGM(m_szMusicPathOrigin[14]);

		struct _stat64i32 temp;
		if (_stat64i32(m_szMusicPath[nIndex], &temp))
			PlayBGM(m_szMusicPathOrigin[nIndex]);
		else
			PlayBGM(m_szMusicPath[nIndex]);

		if (GetVolume(0) != -10000)
			SetVolume(0, m_lBGMVolume);
	}
}

void DS_SOUND_MANAGER::PlayMusic2(int nIndex)
{
	m_nCastleIndex = nIndex;

	if (nIndex >= 0 && nIndex < 15)
	{
		StopBGM();

		if (nIndex == 13)
			ConvertBGM(m_szMusicPathOrigin[13]);
		else if (nIndex == 14)
			ConvertBGM(m_szMusicPathOrigin[14]);

		struct _stat64i32 temp;
		if (_stat64i32(m_szMusicPath[nIndex], &temp))
			PlayBGM(m_szMusicPathOrigin[nIndex]);
		else
			PlayBGM(m_szMusicPath[nIndex]);

		if (GetVolume(0) != -10000)
			SetVolume(0, m_lBGMVolume);
	}
}

void DS_SOUND_MANAGER::PlayASF(char* szURL)
{
	// not used
}

void DS_SOUND_MANAGER::StopASF()
{
	// not used
}

void DS_SOUND_MANAGER::OnEvent()
{
#if defined(__EMSCRIPTEN__)
	return;
#else
	channels->OnEvent();
#endif
}

HRESULT DS_SOUND_MANAGER::RunAll()
{
	// not used
	return E_NOTIMPL;
}

HRESULT DS_SOUND_MANAGER::StopAll()
{
	// not used
	return E_NOTIMPL;
}

HRESULT DS_SOUND_MANAGER::PauseAll()
{
	// not used
	return E_NOTIMPL;
}

HRESULT DS_SOUND_MANAGER::RunSounds()
{
	// not used
	return E_NOTIMPL;
}

HRESULT DS_SOUND_MANAGER::StopSounds()
{
	// not used
	return E_NOTIMPL;
}

HRESULT DS_SOUND_MANAGER::PauseSounds()
{
	return E_NOTIMPL;
}

HRESULT DS_SOUND_MANAGER::StopBGM()
{
#if defined(__EMSCRIPTEN__)
	return wyd_audio_stop_music() ? S_OK : E_FAIL;
#else
	return channels->Stop();
#endif
}

HRESULT DS_SOUND_MANAGER::Run()
{
	return E_NOTIMPL;
}

HRESULT DS_SOUND_MANAGER::Stop()
{
	return E_NOTIMPL;
}

HRESULT DS_SOUND_MANAGER::Pause()
{
	return E_NOTIMPL;
}

HRESULT DS_SOUND_MANAGER::SetEntVolume()
{
	return E_NOTIMPL;
}

HRESULT DS_SOUND_MANAGER::SetEntBalance()
{
	return E_NOTIMPL;
}

HRESULT DS_SOUND_MANAGER::SetVolume(const int which, const int vol)
{
#if defined(__EMSCRIPTEN__)
	if (which != 0)
		return E_INVALIDARG;
	m_lBGMVolume = vol;
	return wyd_audio_set_music_volume(vol) ? S_OK : E_FAIL;
#else
	return channels[which].SetVolume(vol);
#endif
}

int DS_SOUND_MANAGER::GetVolume(const int which)
{
#if defined(__EMSCRIPTEN__)
	return which == 0 ? wyd_audio_get_music_volume() : -10000;
#else
	long vol;
	channels[which].GetVolume(&vol);

	return vol;
#endif
}
