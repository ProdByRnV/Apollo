#include "RealTime/AudioThreadMonitor.h"

#include <atomic>
#include <cstdlib>
#include <new>

//==============================================================================
// Which sanitizer, if any, this binary was built with.

#if defined(__has_feature)
  #if __has_feature(realtime_sanitizer)
    #define APOLLO_REALTIME_SANITIZER 1
  #endif
  #if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer) || __has_feature(memory_sanitizer)
    #define APOLLO_ALLOCATING_SANITIZER 1
  #endif
#endif

#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
  #define APOLLO_ALLOCATING_SANITIZER 1
#endif

#if ! defined(APOLLO_REALTIME_SANITIZER) && ! defined(APOLLO_ALLOCATING_SANITIZER)
  #define APOLLO_MONITOR_REPLACES_NEW 1
#endif

#if defined(APOLLO_REALTIME_SANITIZER)
// RTSan's own entry points, which [[clang::nonblocking]] functions call on the
// way in and out. Declared here rather than taken from its header because the
// header does not declare them: they are the runtime's, and the attribute is
// the documented way in. The attribute would also put every call the section
// makes through Clang's compile-time effect analysis, which is a different
// tool asking a stricter question than this one does.
extern "C" void __rtsan_realtime_enter();
extern "C" void __rtsan_realtime_exit();
#endif

namespace apollo::realtime
{
namespace
{

/** How deep in sections this thread is. A trivial thread_local, so reading it
    from inside operator new allocates nothing. */
thread_local int sectionDepth = 0;

/** Set while the monitor itself is allocating, to report what it saw, so that
    its own work is not counted as the audio thread's. */
thread_local int reporting = 0;

std::atomic<std::uint64_t> allocationCount { 0 };
std::atomic<std::uint64_t> deallocationCount { 0 };
std::atomic<bool> stackTaken { false };

juce::SpinLock stackLock;
juce::String firstStack;

} // namespace

#if defined(APOLLO_MONITOR_REPLACES_NEW)
namespace detail
{

/** Counts one event if this thread is inside a section, and for the first
    since the last takeFindings(), remembers where it happened. */
void note (std::atomic<std::uint64_t>& counter) noexcept
{
    if (sectionDepth == 0 || reporting != 0)
        return;

    counter.fetch_add (1, std::memory_order_relaxed);

    if (stackTaken.exchange (true, std::memory_order_acq_rel))
        return;

    ++reporting;

    try
    {
        auto stack = juce::SystemStats::getStackBacktrace();
        const juce::SpinLock::ScopedLockType hold (stackLock);
        firstStack = std::move (stack);
    }
    catch (...)
    {
    }

    --reporting;
}

} // namespace detail
#endif

Coverage coverage() noexcept
{
    Coverage result;

#if defined(APOLLO_REALTIME_SANITIZER)
    result.cppAllocation = true;
    result.mallocFamily = true;
    result.locks = true;
    result.sanitizer = true;
#elif defined(APOLLO_MONITOR_REPLACES_NEW)
    result.cppAllocation = true;
#endif

    return result;
}

juce::String describeCoverage()
{
    const auto seen = coverage();

    if (seen.sanitizer)
        return "RealtimeSanitizer: every allocation, free, lock and blocking call, anywhere in the call tree; "
               "the first one ends the run";

    if (seen.cppAllocation)
        return "operator new and delete: every allocation C++ makes through new, counted; "
               "not malloc called directly, and not locks (those are the RealtimeSanitizer job's)";

    return "nothing: this build runs under a sanitizer that replaces operator new itself";
}

Findings takeFindings()
{
    // The monitor's own allocations here are not the audio thread's.
    ++reporting;

    Findings findings;
    findings.allocations = allocationCount.exchange (0, std::memory_order_relaxed);
    findings.deallocations = deallocationCount.exchange (0, std::memory_order_relaxed);

    {
        const juce::SpinLock::ScopedLockType hold (stackLock);
        findings.firstStack = std::move (firstStack);
        firstStack = {};
    }

    stackTaken.store (false, std::memory_order_release);

    --reporting;
    return findings;
}

AudioThreadSection::AudioThreadSection (Checks checks) noexcept
{
    ++sectionDepth;

#if defined(APOLLO_REALTIME_SANITIZER)
    if (checks == Checks::everything)
    {
        __rtsan_realtime_enter();
        enteredSanitizer = true;
    }
#else
    juce::ignoreUnused (checks);
#endif
}

AudioThreadSection::~AudioThreadSection()
{
#if defined(APOLLO_REALTIME_SANITIZER)
    if (enteredSanitizer)
        __rtsan_realtime_exit();
#endif

    --sectionDepth;
}

namespace
{
/** Where the canary's allocation is stored, so that no optimiser can prove it
    unused and leave the allocation out. A new-expression may legally be
    elided; a call to operator new whose result escapes may not. */
void* volatile canarySink = nullptr;
} // namespace

void allocateInsideASection()
{
    const AudioThreadSection section;
    canarySink = ::operator new (64);
    ::operator delete (canarySink);
    canarySink = nullptr;
}

} // namespace apollo::realtime

//==============================================================================
// The replacement operators.
//
// Every form the standard lets a program replace, so that no allocation slips
// through a variant nobody thought of: plain, array, nothrow, aligned, and the
// sized deletes C++14 added. They allocate with malloc, exactly as the default
// ones do, and differ only in counting.

#if defined(APOLLO_MONITOR_REPLACES_NEW)
namespace
{

void* allocate (std::size_t size)
{
    apollo::realtime::detail::note (apollo::realtime::allocationCount);

    if (size == 0)
        size = 1;

    for (;;)
    {
        if (auto* memory = std::malloc (size))
            return memory;

        if (auto handler = std::get_new_handler())
            handler();
        else
            throw std::bad_alloc();
    }
}

void* allocateAligned (std::size_t size, std::align_val_t alignment)
{
    apollo::realtime::detail::note (apollo::realtime::allocationCount);

    if (size == 0)
        size = 1;

    auto align = static_cast<std::size_t> (alignment);

    if (align < sizeof (void*))
        align = sizeof (void*);

    for (;;)
    {
       #if defined(_MSC_VER)
        if (auto* memory = _aligned_malloc (size, align))
            return memory;
       #else
        void* memory = nullptr;

        if (posix_memalign (&memory, align, size) == 0)
            return memory;
       #endif

        if (auto handler = std::get_new_handler())
            handler();
        else
            throw std::bad_alloc();
    }
}

void release (void* memory) noexcept
{
    if (memory == nullptr)
        return;

    apollo::realtime::detail::note (apollo::realtime::deallocationCount);
    std::free (memory);
}

void releaseAligned (void* memory) noexcept
{
    if (memory == nullptr)
        return;

    apollo::realtime::detail::note (apollo::realtime::deallocationCount);

   #if defined(_MSC_VER)
    _aligned_free (memory);
   #else
    std::free (memory);
   #endif
}

template <typename Allocate>
void* withoutThrowing (Allocate&& allocateFn) noexcept
{
    try
    {
        return allocateFn();
    }
    catch (...)
    {
        return nullptr;
    }
}

} // namespace

void* operator new (std::size_t size) { return allocate (size); }
void* operator new[] (std::size_t size) { return allocate (size); }
void* operator new (std::size_t size, std::align_val_t al) { return allocateAligned (size, al); }
void* operator new[] (std::size_t size, std::align_val_t al) { return allocateAligned (size, al); }

void* operator new (std::size_t size, const std::nothrow_t&) noexcept
{
    return withoutThrowing ([size] { return allocate (size); });
}

void* operator new[] (std::size_t size, const std::nothrow_t&) noexcept
{
    return withoutThrowing ([size] { return allocate (size); });
}

void* operator new (std::size_t size, std::align_val_t al, const std::nothrow_t&) noexcept
{
    return withoutThrowing ([size, al] { return allocateAligned (size, al); });
}

void* operator new[] (std::size_t size, std::align_val_t al, const std::nothrow_t&) noexcept
{
    return withoutThrowing ([size, al] { return allocateAligned (size, al); });
}

void operator delete (void* memory) noexcept { release (memory); }
void operator delete[] (void* memory) noexcept { release (memory); }
void operator delete (void* memory, std::size_t) noexcept { release (memory); }
void operator delete[] (void* memory, std::size_t) noexcept { release (memory); }
void operator delete (void* memory, const std::nothrow_t&) noexcept { release (memory); }
void operator delete[] (void* memory, const std::nothrow_t&) noexcept { release (memory); }

void operator delete (void* memory, std::align_val_t) noexcept { releaseAligned (memory); }
void operator delete[] (void* memory, std::align_val_t) noexcept { releaseAligned (memory); }
void operator delete (void* memory, std::size_t, std::align_val_t) noexcept { releaseAligned (memory); }
void operator delete[] (void* memory, std::size_t, std::align_val_t) noexcept { releaseAligned (memory); }
void operator delete (void* memory, std::align_val_t, const std::nothrow_t&) noexcept { releaseAligned (memory); }
void operator delete[] (void* memory, std::align_val_t, const std::nothrow_t&) noexcept { releaseAligned (memory); }
#endif
