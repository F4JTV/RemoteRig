/*
 * Minimal <pthread.h> shim for building the Hamlib headers with MSVC.
 *
 * hamlib/rig.h includes <pthread.h> unconditionally, and MSVC ships none.
 * Hamlib's own comment suggests installing the NuGet pthreads package, but
 * only two types are actually needed: pthread_t and pthread_mutex_t, both used
 * as members of struct rig_state.
 *
 * The official Hamlib Windows DLL is cross-compiled with MinGW against
 * winpthreads, where those types are:
 *
 *     typedef uintptr_t pthread_t;        // 8 bytes on x64
 *     typedef intptr_t  pthread_mutex_t;  // 8 bytes on x64
 *
 * Matching them exactly keeps the structure layout identical to the DLL's, so
 * no field lands at the wrong offset. Nothing here is ever called: this file
 * only has to make the headers parse.
 *
 * Used solely when compiling with MSVC. On Linux, macOS and MinGW the real
 * pthread.h is used and this file is never reached.
 */
#ifndef REMOTERIG_MSVC_PTHREAD_SHIM_H
#define REMOTERIG_MSVC_PTHREAD_SHIM_H

#if !defined(_MSC_VER)
#error "This shim is only meant for MSVC. Use the platform's real pthread.h."
#endif

#include <stdint.h>

/* Keep a real pthread implementation from being layered on top of this one. */
#ifndef WIN_PTHREADS_H
#define WIN_PTHREADS_H
#endif
#ifndef PTHREAD_H
#define PTHREAD_H
#endif

/* Exactly the two types the Hamlib headers refer to, nothing more:
   a narrower shim means fewer chances of clashing with anything else. */
typedef uintptr_t pthread_t;
typedef intptr_t  pthread_mutex_t;

#endif /* REMOTERIG_MSVC_PTHREAD_SHIM_H */
