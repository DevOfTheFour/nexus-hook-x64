#include "NexusHook.h"

// Initialization
bool NexusHook::Init()
{

	// Setup SwapChain
	if (!hMngr.CreateDeviceAndSwapChain())
		return false;
	if (!hMngr.FindSwapChain())
		return false;

	return true;
}

// Hooking
bool NexusHook::HookSwapChain(
	std::uintptr_t newFunc,
	int index)
{
	constexpr std::size_t SwapChainVTableSize =
		SC_GETLASTPRESENTCOUNT + 1;

	hkHooks[index] =
		VMTHook(hMngr.pSwapChain, SwapChainVTableSize);

	oFunctions[index] =
		hkHooks[index].Hook(
			newFunc,
			SC_PRESENT);

	return oFunctions[index] != 0;
}