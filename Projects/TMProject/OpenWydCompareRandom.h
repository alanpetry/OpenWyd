#pragma once

#include <cstdint>

#if defined(__EMSCRIPTEN__) || \
	(defined(OPENWYD_COMPARE) && defined(_DEBUG))

// The original Win32 client uses the Microsoft CRT linear-congruential
// generator. Keep the transition as a pure constexpr operation so both the
// Win32 and WASM comparison paths compile the exact same algorithm.
constexpr std::uint32_t OpenWydCompareMsVcrtRandomNextState(
	std::uint32_t state)
{
	return state * 214013u + 2531011u;
}

constexpr int OpenWydCompareMsVcrtRandomValue(std::uint32_t state)
{
	return static_cast<int>((state >> 16u) & 0x7FFFu);
}

static_assert(
	OpenWydCompareMsVcrtRandomNextState(1u) == 2745024u,
	"MSVCRT random state transition changed");
static_assert(
	OpenWydCompareMsVcrtRandomValue(
		OpenWydCompareMsVcrtRandomNextState(1u)) == 41,
	"MSVCRT random first value changed");
static_assert(
	OpenWydCompareMsVcrtRandomValue(
		OpenWydCompareMsVcrtRandomNextState(
			OpenWydCompareMsVcrtRandomNextState(1u))) == 18467,
	"MSVCRT random second value changed");

// These wrappers fall through to the platform CRT until explicitly armed.
// While armed, every client srand() request resets to the externally selected
// comparison seed, so distinct server clocks cannot split the two sequences.
int OpenWydCompareRandomRand();
void OpenWydCompareRandomSrand(unsigned int requestedSeed);
void OpenWydCompareRandomArm(unsigned int seed);
void OpenWydCompareRandomDisarm();
bool OpenWydCompareRandomIsArmed();
unsigned int OpenWydCompareRandomConfiguredSeed();
unsigned int OpenWydCompareRandomState();
unsigned int OpenWydCompareRandomRandCalls();
unsigned int OpenWydCompareRandomSrandCalls();
unsigned int OpenWydCompareRandomLastRequestedSeed();

extern "C"
{
	int wyd_compare_random_arm(unsigned int seed);
	int wyd_compare_random_disarm();
	int wyd_compare_random_is_armed();
	unsigned int wyd_compare_random_configured_seed();
	unsigned int wyd_compare_random_state();
	unsigned int wyd_compare_random_rand_calls();
	unsigned int wyd_compare_random_srand_calls();
	unsigned int wyd_compare_random_last_requested_seed();
	int wyd_compare_random_next_for_test();
	void wyd_compare_random_srand_for_test(unsigned int requestedSeed);
}

#endif
