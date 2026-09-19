#include "SwapChainManager.h"

#include <intrin.h> // __cpuid, _xgetbv, SSE2 / AVX2 intrinsics

// Creates a temponary swapchain for later use
bool _stdcall SwapChainManager::CreateDeviceAndSwapChain()
{

	// Get handle of game
	HWND hWnd = GetForegroundWindow();
	if (hWnd == NULL)
		return false;

	// Create SwapChain description
	DXGI_SWAP_CHAIN_DESC SwapChainDesc;
	memset(&SwapChainDesc, 0, sizeof(struct DXGI_SWAP_CHAIN_DESC));
	SwapChainDesc.BufferCount = 1;
	SwapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	SwapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	SwapChainDesc.OutputWindow = hWnd;
	SwapChainDesc.SampleDesc.Count = 1;
	SwapChainDesc.Windowed = (GetWindowLong(hWnd, GWL_STYLE) & WS_POPUP) != 0 ? FALSE : TRUE;
	SwapChainDesc.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
	SwapChainDesc.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
	SwapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

	// Create SwapChain feature level
	D3D_FEATURE_LEVEL SwapChainFeatureLevel[1];
	SwapChainFeatureLevel[0] = D3D_FEATURE_LEVEL_11_0;

	// Create SwapChain
	HRESULT SwapChainResult = D3D11CreateDeviceAndSwapChain(
		NULL,					   // Adapter
		D3D_DRIVER_TYPE_HARDWARE,  // Driver
		NULL,					   // Software
		0,						   // Flags
		SwapChainFeatureLevel,	   // Feature level
		1,						   // Number of feature levels
		D3D11_SDK_VERSION,		   // SDK Version
		&SwapChainDesc,			   // SwapChain description
		&pTempSwapChain,		   // SwapChain output
		&pTempDevice,			   // Device output
		NULL,					   // Discard feature level result
		&pTempContext			   // Context output
	);

	// Error handling
	if (FAILED(SwapChainResult))
	{
		_com_error err(SwapChainResult);
#ifdef _DEBUG
		std::cout << "SwapChain creation failed: " << err.ErrorMessage() << std::endl;
#endif
		return false;
	}

	// Get Vtable
	pSwapChainVtable = (DWORD_PTR *)pTempSwapChain;
	pSwapChainVtable = (DWORD_PTR *)pSwapChainVtable[0];
#ifdef _DEBUG
	std::cout << "SwapChain created. Vtable: 0x" << std::hex << pSwapChainVtable << std::endl;
#endif
	return true;
}

// ============================================================================
//  SIMD scanning helpers
// ============================================================================

namespace
{
	// ------------------------------------------------------------------
	// Detects AVX2 support once (including OS XSAVE enablement).
	// ------------------------------------------------------------------
	bool HasAvx2() noexcept
	{
		static const bool supported = []() noexcept
		{
			int regs[4];

			__cpuid(regs, 0);
			if (regs[0] < 7) // CPUID leaf 7 unsupported
				return false;

			__cpuid(regs, 1);
			if (!(regs[2] & (1 << 27))) // no OSXSAVE
				return false;

			__cpuid(regs, 7);
			if (!(regs[1] & (1 << 5))) // no AVX2
				return false;

			// The OS must actually preserve YMM state across context switches.
			const unsigned long long xcr0 = _xgetbv(0);
			return (xcr0 & 0x6) == 0x6; // XMM | YMM enabled
		}();

		return supported;
	}

	// ------------------------------------------------------------------
	// Scalar fallback for region tails that don't fill a full vector.
	// ------------------------------------------------------------------
	const DWORD_PTR *FindScalar(const DWORD_PTR *p, const DWORD_PTR *pEnd, DWORD_PTR value) noexcept
	{
		for (; p < pEnd; ++p)
			if (*p == value)
				return p;
		return nullptr;
	}

#ifdef _AMD64_
	// ============================ x64 paths ============================

	// SSE2 (guaranteed on x64): 2 pointers (16 bytes) per iteration.
	const DWORD_PTR *FindValueSse2(const DWORD_PTR *pBegin, const DWORD_PTR *pEnd, DWORD_PTR value) noexcept
	{
		// Broadcast the needle into both 64-bit lanes.
		const __m128i needle = _mm_set1_epi64x(static_cast<long long>(value));

		for (; pBegin + 2 <= pEnd; pBegin += 2)
		{
			const __m128i block = _mm_loadu_si128(reinterpret_cast<const __m128i *>(pBegin));

			// NOTE: _mm_cmpeq_epi64 is SSE4.1, so a 64-bit compare is
			// emulated in pure SSE2: compare per-dword, then AND the result
			// with itself dword-swapped inside each 64-bit lane. A lane only
			// survives when BOTH of its dwords matched.
			__m128i eq = _mm_cmpeq_epi32(block, needle);
			eq = _mm_and_si128(eq, _mm_shuffle_epi32(eq, _MM_SHUFFLE(2, 3, 0, 1)));

			// movemask is 0x00FF for lane 0, 0xFF00 for lane 1.
			const unsigned mask = static_cast<unsigned>(_mm_movemask_epi8(eq));
			if (mask == 0)
				continue;

			return pBegin + (mask & 0xFF ? 0 : 1);
		}

		// Tail: a single pointer left over.
		return (pBegin < pEnd && *pBegin == value) ? pBegin : nullptr;
	}

	// AVX2: 4 pointers (32 bytes) per iteration.
	const DWORD_PTR *FindValueAvx2(const DWORD_PTR *pBegin, const DWORD_PTR *pEnd, DWORD_PTR value) noexcept
	{
		const __m256i needle = _mm256_set1_epi64x(static_cast<long long>(value));

		for (; pBegin + 4 <= pEnd; pBegin += 4)
		{
			const __m256i block = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(pBegin));
			const unsigned mask = static_cast<unsigned>(
				_mm256_movemask_epi8(_mm256_cmpeq_epi64(block, needle)));

			if (mask == 0)
				continue;

			// Each matched lane sets 8 consecutive bits.
			int lane = 0;
			for (unsigned bits = mask; (bits & 0xFF) == 0; bits >>= 8)
				++lane;
			return pBegin + lane;
		}

		return FindScalar(pBegin, pEnd, value);
	}

#else
	// ============================ x86 paths ============================

	// SSE2: 4 pointers (16 bytes) per iteration.
	const DWORD_PTR *FindValueSse2(const DWORD_PTR *pBegin, const DWORD_PTR *pEnd, DWORD_PTR value) noexcept
	{
		const __m128i needle = _mm_set1_epi32(static_cast<int>(value));

		for (; pBegin + 4 <= pEnd; pBegin += 4)
		{
			const __m128i block = _mm_loadu_si128(reinterpret_cast<const __m128i *>(pBegin));
			const unsigned mask = static_cast<unsigned>(
				_mm_movemask_epi8(_mm_cmpeq_epi32(block, needle)));

			if (mask == 0)
				continue;

			// Each matched lane sets 4 consecutive bits.
			int lane = 0;
			for (unsigned bits = mask; (bits & 0xF) == 0; bits >>= 4)
				++lane;
			return pBegin + lane;
		}

		return FindScalar(pBegin, pEnd, value);
	}

	// AVX2: 8 pointers (32 bytes) per iteration.
	const DWORD_PTR *FindValueAvx2(const DWORD_PTR *pBegin, const DWORD_PTR *pEnd, DWORD_PTR value) noexcept
	{
		const __m256i needle = _mm256_set1_epi32(static_cast<int>(value));

		for (; pBegin + 8 <= pEnd; pBegin += 8)
		{
			const __m256i block = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(pBegin));
			const unsigned mask = static_cast<unsigned>(
				_mm256_movemask_epi8(_mm256_cmpeq_epi32(block, needle)));

			if (mask == 0)
				continue;

			int lane = 0;
			for (unsigned bits = mask; (bits & 0xF) == 0; bits >>= 4)
				++lane;
			return pBegin + lane;
		}

		return FindScalar(pBegin, pEnd, value);
	}

#endif

	// ------------------------------------------------------------------
	// Dispatch: AVX2 when available, SSE2 otherwise.
	// Returns the first element equal to 'value' inside [pBegin, pEnd).
	// ------------------------------------------------------------------
	const DWORD_PTR *FindValue(const DWORD_PTR *pBegin, const DWORD_PTR *pEnd, DWORD_PTR value) noexcept
	{
		if (HasAvx2())
			return FindValueAvx2(pBegin, pEnd, value);
		return FindValueSse2(pBegin, pEnd, value);
	}
} // namespace

// ============================================================================
//  FindSwapChain
// ============================================================================

// Scans the process address space for a COM object whose first member (the
// vtable pointer) matches the vtable of our dummy swap chain - i.e. the
// game's live IDXGISwapChain instance.
//
// Why this is fast compared to the naive version:
//   1. Only MEM_PRIVATE + READWRITE pages are scanned: the object lives on
//      the heap, so images, mapped files and executable code are skipped.
//   2. The inner loop advances by pointer size, not by 1 byte.
//   3. The comparison itself is SIMD: 2-8 pointers per instruction.
//   4. SEH guards a whole region instead of every single read.
bool SwapChainManager::FindSwapChain()
{
	// Create the dummy swap chain (and capture its vtable) if not done yet.
	if (pSwapChainVtable == nullptr && !CreateDeviceAndSwapChain())
		return false;

	const DWORD_PTR wantedVtable = reinterpret_cast<DWORD_PTR>(pSwapChainVtable);
	const DWORD_PTR tempObject = reinterpret_cast<DWORD_PTR>(pTempSwapChain);

#ifdef _AMD64_
	constexpr DWORD_PTR kScanLimit = 0x7FFFFFFEFFFFULL;
#else
	constexpr DWORD_PTR kScanLimit = 0xFFE00000UL;
#endif

	MEMORY_BASIC_INFORMATION mbi{};

	for (DWORD_PTR address = 0x10000; address < kScanLimit;)
	{
		// NOTE: 'break', not 'continue' - if VirtualQuery fails, 'address'
		// would never advance and the loop would spin forever.
		if (VirtualQuery(reinterpret_cast<LPCVOID>(address), &mbi, sizeof(mbi)) == 0)
			break;

		address = reinterpret_cast<DWORD_PTR>(mbi.BaseAddress) + mbi.RegionSize;

		// ---- Filter: keep only committed, private, writable data pages ----
		if (mbi.State != MEM_COMMIT)
			continue;
		if (mbi.Type != MEM_PRIVATE) // heap / VirtualAlloc only
			continue;
		if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))
			continue;
		if ((mbi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE)) == 0)
			continue;

		// ---- Align region bounds to pointer granularity ----
		const DWORD_PTR regionBegin =
			(reinterpret_cast<DWORD_PTR>(mbi.BaseAddress) + sizeof(DWORD_PTR) - 1) & ~static_cast<DWORD_PTR>(sizeof(DWORD_PTR) - 1);
		const DWORD_PTR regionEnd =
			reinterpret_cast<DWORD_PTR>(mbi.BaseAddress) + mbi.RegionSize;

		if (regionBegin >= regionEnd)
			continue;

		__try
		{
			const DWORD_PTR *pCursor = reinterpret_cast<const DWORD_PTR *>(regionBegin);
			const DWORD_PTR *pEnd = reinterpret_cast<const DWORD_PTR *>(regionEnd);

			while (pCursor < pEnd)
			{
				const DWORD_PTR *pHit = FindValue(pCursor, pEnd, wantedVtable);
				if (pHit == nullptr)
					break; // no match in this region

				// Resume after this hit in case the candidate turns out invalid.
				pCursor = pHit + 1;

				IDXGISwapChain *pCandidate =
					reinterpret_cast<IDXGISwapChain *>(const_cast<DWORD_PTR *>(pHit));

				// Skip our own temporary swap chain.
				if (reinterpret_cast<DWORD_PTR>(pCandidate) == tempObject)
					continue;

				// ---- Validate the candidate ----
				// GetDevice filters out stale / bogus pointers that merely
				// happen to store the same vtable value.
				ID3D11Device *pFoundDevice = nullptr;
				if (FAILED(pCandidate->GetDevice(__uuidof(ID3D11Device),
												 reinterpret_cast<LPVOID *>(&pFoundDevice))))
					continue;

				ID3D11DeviceContext *pFoundContext = nullptr;
				pFoundDevice->GetImmediateContext(&pFoundContext);
				if (pFoundContext == nullptr)
				{
					pFoundDevice->Release();
					continue;
				}

				// Success - store everything and drop the dummy objects.
				pSwapChain = pCandidate;
				pDevice = pFoundDevice;
				pContext = pFoundContext;

				ReleaseTempDevices();

#ifdef _DEBUG
				std::cout << "Found swap chain: 0x" << std::hex
						  << reinterpret_cast<void *>(pSwapChain) << std::endl;
#endif
				return true;
			}
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			// The region was decommitted underneath us - just move on.
			// Inside committed RW pages within [begin, end) a fault is
			// practically impossible, this is a pure safety net.
		}
	}

#ifdef _DEBUG
	std::cout << "Couldn't find swap chain" << std::endl;
#endif
	return false;
}

// Release used resources
void SwapChainManager::ReleaseTempDevices()
{
	pTempSwapChain->Release();
	pTempDevice->Release();
	pTempContext->Release();
}