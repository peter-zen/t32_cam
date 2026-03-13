#ifndef MDNS_CONFIG_H
#define MDNS_CONFIG_H

#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 500
#endif

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE 1
#endif

#if defined(__APPLE__)
#define _DARWIN_C_SOURCE 1
#endif

#if defined(__GNUC__) || defined(__clang__)
#define MDNS_EXPORT __attribute__((visibility("default")))
#else
#define MDNS_EXPORT
#endif

#ifdef _MSC_VER
#define MDNS_INLINE __inline
#else
#define MDNS_INLINE inline
#endif

#endif
