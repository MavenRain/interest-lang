/* The domain file, embedded in langc by gen/embed.c (build/domain.c), so
 * langc reads no domain file at run time. */
#ifndef LANG_PRELUDE_H
#define LANG_PRELUDE_H
#include <stddef.h>

#define LANG_PRELUDE_NAME "domain/domain.lang"
extern const unsigned char domain_source[]; /* the bytes, then one NUL */
extern const size_t domain_source_len;      /* the bytes without the NUL */
#endif
