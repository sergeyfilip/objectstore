//
// Selector for thread implementations
//

#if defined(__unix__) || defined(__APPLE__)
# include "thread_posix.cc"
#endif

#ifdef _WIN32
# include "thread_win32.cc"
#endif

