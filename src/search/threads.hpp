#pragma once
// Is std::thread usable? libstdc++ without gthreads (a MinGW "win32 threads" build) declares neither std::thread nor
// std::mutex, so detect that and fall back to a single-threaded engine. Define MATEX_NO_THREADS to force that.
#if defined(MATEX_NO_THREADS) || (defined(__GLIBCXX__) && !defined(_GLIBCXX_HAS_GTHREADS))
#define MATEX_THREADS 0
#else
#define MATEX_THREADS 1
#endif

#if MATEX_THREADS
#include <mutex>
#include <thread>
#endif
