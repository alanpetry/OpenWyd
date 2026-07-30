#include "pch.h"

#if defined(__EMSCRIPTEN__) || \
	(defined(OPENWYD_COMPARE) && defined(_DEBUG)) || \
	defined(OPENWYD_LAB)

// pch.h redirects client rand/srand calls. This implementation needs the real
// platform CRT as the exact inactive-mode fallback.
#undef rand
#undef srand

#include "OpenWydCompareRandom.h"

#include <cstdlib>

namespace
{
	struct CompareRandomState
	{
		bool armed = false;
		std::uint32_t configuredSeed = 1u;
		std::uint32_t state = 1u;
		std::uint32_t randCalls = 0u;
		std::uint32_t srandCalls = 0u;
		std::uint32_t lastRequestedSeed = 0u;
	};

	CompareRandomState g_compareRandom;
}

int OpenWydCompareRandomRand()
{
	if (!g_compareRandom.armed)
		return std::rand();

	g_compareRandom.state =
		OpenWydCompareMsVcrtRandomNextState(g_compareRandom.state);
	++g_compareRandom.randCalls;
	return OpenWydCompareMsVcrtRandomValue(g_compareRandom.state);
}

void OpenWydCompareRandomSrand(unsigned int requestedSeed)
{
	if (!g_compareRandom.armed)
	{
		std::srand(requestedSeed);
		return;
	}

	g_compareRandom.lastRequestedSeed = requestedSeed;
	g_compareRandom.state = g_compareRandom.configuredSeed;
	++g_compareRandom.srandCalls;
}

void OpenWydCompareRandomArm(unsigned int seed)
{
	g_compareRandom.armed = true;
	g_compareRandom.configuredSeed = seed;
	g_compareRandom.state = seed;
	g_compareRandom.randCalls = 0u;
	g_compareRandom.srandCalls = 0u;
	g_compareRandom.lastRequestedSeed = 0u;
}

void OpenWydCompareRandomDisarm()
{
	g_compareRandom.armed = false;
}

bool OpenWydCompareRandomIsArmed()
{
	return g_compareRandom.armed;
}

unsigned int OpenWydCompareRandomConfiguredSeed()
{
	return g_compareRandom.configuredSeed;
}

unsigned int OpenWydCompareRandomState()
{
	return g_compareRandom.state;
}

unsigned int OpenWydCompareRandomRandCalls()
{
	return g_compareRandom.randCalls;
}

unsigned int OpenWydCompareRandomSrandCalls()
{
	return g_compareRandom.srandCalls;
}

unsigned int OpenWydCompareRandomLastRequestedSeed()
{
	return g_compareRandom.lastRequestedSeed;
}

extern "C" int wyd_compare_random_arm(unsigned int seed)
{
	OpenWydCompareRandomArm(seed);
	return 1;
}

extern "C" int wyd_compare_random_disarm()
{
	OpenWydCompareRandomDisarm();
	return 1;
}

extern "C" int wyd_compare_random_is_armed()
{
	return OpenWydCompareRandomIsArmed() ? 1 : 0;
}

extern "C" unsigned int wyd_compare_random_configured_seed()
{
	return OpenWydCompareRandomConfiguredSeed();
}

extern "C" unsigned int wyd_compare_random_state()
{
	return OpenWydCompareRandomState();
}

extern "C" unsigned int wyd_compare_random_rand_calls()
{
	return OpenWydCompareRandomRandCalls();
}

extern "C" unsigned int wyd_compare_random_srand_calls()
{
	return OpenWydCompareRandomSrandCalls();
}

extern "C" unsigned int wyd_compare_random_last_requested_seed()
{
	return OpenWydCompareRandomLastRequestedSeed();
}

extern "C" int wyd_compare_random_next_for_test()
{
	return OpenWydCompareRandomRand();
}

extern "C" void wyd_compare_random_srand_for_test(
	unsigned int requestedSeed)
{
	OpenWydCompareRandomSrand(requestedSeed);
}

#endif
