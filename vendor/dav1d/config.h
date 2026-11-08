// goose-image: hand-written dav1d build configuration. Pure C, no asm,
// both 8 and 16 bit paths. Matches the meson defaults for a plain build.
#ifndef DAV1D_CONFIG_H
#define DAV1D_CONFIG_H

#define CONFIG_8BPC 1
#define CONFIG_16BPC 1
#define CONFIG_LOG 1
#define CONFIG_MACOS_KPERF 0
#define TRIM_DSP_FUNCTIONS 0

#define HAVE_ASM 0
#define HAVE_ALIGNED_ALLOC 1
#define HAVE_MEMALIGN 1
#define HAVE_POSIX_MEMALIGN 1
#define HAVE_UNISTD_H 1
#define HAVE_DLSYM 0
#define HAVE_CLOCK_GETTIME 1

#define HAVE_GETAUXVAL 0
#define HAVE_ELF_AUX_INFO 0
#define HAVE_AUX 0
#define HAVE_PTHREAD_GETAFFINITY_NP 0
#define HAVE_PTHREAD_NP_H 0
#define HAVE_PTHREAD_SETNAME_NP 0
#define HAVE_PTHREAD_SET_NAME_NP 0
#define HAVE_AS_ARCH_DIRECTIVE 0
#define HAVE_AS_ARCHEXT_SVE_DIRECTIVE 0
#define HAVE_AS_ARCHEXT_SVE2_DIRECTIVE 0
#define HAVE_AS_ARCHEXT_DOTPROD_DIRECTIVE 0
#define HAVE_AS_ARCHEXT_I8MM_DIRECTIVE 0
#define HAVE_AS_FUNC 0
#define HAVE_SVE2 0
#define HAVE_DOTPROD 0
#define HAVE_I8MM 0
#define HAVE_DECL_GETOPT 0

#endif
