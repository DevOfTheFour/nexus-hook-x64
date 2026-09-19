#include "VMTHook.h"

#include <Windows.h>

VMTHook::VMTHook(void* object, std::size_t count)
{
    if (!object || count == 0)
        return;

    m_ppVTable =
        reinterpret_cast<VTableEntry**>(object);

    m_pOldVTable = *m_ppVTable;

    if (!m_pOldVTable)
    {
        m_ppVTable = nullptr;
        return;
    }

    m_count = count;

    m_pNewVTable = new VTableEntry[m_count];

    memcpy(
        m_pNewVTable,
        m_pOldVTable,
        m_count * sizeof(VTableEntry)
    );

    *m_ppVTable = m_pNewVTable;
}

VMTHook::VMTHook(VMTHook&& other) noexcept
    : m_count(other.m_count),
      m_ppVTable(other.m_ppVTable),
      m_pOldVTable(other.m_pOldVTable),
      m_pNewVTable(other.m_pNewVTable)
{
    other.m_count = 0;
    other.m_ppVTable = nullptr;
    other.m_pOldVTable = nullptr;
    other.m_pNewVTable = nullptr;
}

VMTHook& VMTHook::operator=(VMTHook&& other) noexcept
{
    if (this == &other)
        return *this;

    Reset();

    m_count = other.m_count;
    m_ppVTable = other.m_ppVTable;
    m_pOldVTable = other.m_pOldVTable;
    m_pNewVTable = other.m_pNewVTable;

    other.m_count = 0;
    other.m_ppVTable = nullptr;
    other.m_pOldVTable = nullptr;
    other.m_pNewVTable = nullptr;

    return *this;
}

VMTHook::~VMTHook()
{
    Reset();
}

VMTHook::VTableEntry VMTHook::Hook(
    VTableEntry newFunc,
    std::size_t index)
{
    if (!IsValid() || index >= m_count || newFunc == 0)
        return 0;

    const VTableEntry oldFunc =
        m_pOldVTable[index];

    m_pNewVTable[index] = newFunc;

    return oldFunc;
}

void VMTHook::UnHook()
{
    if (!m_ppVTable)
        return;

    *m_ppVTable = m_pOldVTable;
}

void VMTHook::ReHook()
{
    if (!m_ppVTable || !m_pNewVTable)
        return;

    *m_ppVTable = m_pNewVTable;
}

bool VMTHook::IsValid() const noexcept
{
    return m_ppVTable != nullptr &&
           m_pOldVTable != nullptr &&
           m_pNewVTable != nullptr &&
           m_count > 0;
}

void VMTHook::Reset() noexcept
{
    // Восстанавливаем оригинальную VMT перед освобождением копии.
    if (m_ppVTable && m_pOldVTable)
        *m_ppVTable = m_pOldVTable;

    delete[] m_pNewVTable;

    m_count = 0;
    m_ppVTable = nullptr;
    m_pOldVTable = nullptr;
    m_pNewVTable = nullptr;
}
