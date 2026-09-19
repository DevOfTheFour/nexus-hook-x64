#pragma once

#include <cstddef>
#include <cstdint>

class VMTHook
{
public:
    using VTableEntry = std::uintptr_t;

    VMTHook() = default;
    VMTHook(void* object, std::size_t count);

    VMTHook(const VMTHook&) = delete;
    VMTHook& operator=(const VMTHook&) = delete;

    VMTHook(VMTHook&& other) noexcept;
    VMTHook& operator=(VMTHook&& other) noexcept;

    ~VMTHook();

    VTableEntry Hook(VTableEntry newFunc, std::size_t index);

    void UnHook();
    void ReHook();

    [[nodiscard]]
    bool IsValid() const noexcept;

private:
    void Reset() noexcept;

private:
    std::size_t m_count = 0;

    VTableEntry** m_ppVTable = nullptr;
    VTableEntry* m_pOldVTable = nullptr;
    VTableEntry* m_pNewVTable = nullptr;
};
