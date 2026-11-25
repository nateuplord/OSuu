#ifndef _OSUU_ASSERT_H
#define _OSUU_ASSERT_H
void osuu_assert_fail(const char *expr, const char *file, int line);
#ifdef NDEBUG
#define assert(e) ((void) 0)
#else
#define assert(e) ((e) ? (void) 0 : osuu_assert_fail(#e, __FILE__, __LINE__))
#endif
#endif
