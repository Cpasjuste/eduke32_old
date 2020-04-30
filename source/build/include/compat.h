// Compatibility declarations for things which might not be present in
// certain build environments. It also levels the playing field caused
// by different platforms.

#ifndef compat_h_
#define compat_h_

#pragma once

#ifdef _WIN32
# include "windows_inc.h"
#endif

////////// Compiler detection //////////

#ifdef __GNUC__
# define EDUKE32_GCC_PREREQ(major, minor) (major < __GNUC__ || (major == __GNUC__ && minor <= __GNUC_MINOR__))
#else
# define EDUKE32_GCC_PREREQ(major, minor) 0
#endif

#ifdef __clang__
# define EDUKE32_CLANG_PREREQ(major, minor) (major < __clang_major__ || (major == __clang_major__ && minor <= __clang_minor__))
#else
# define EDUKE32_CLANG_PREREQ(major, minor) 0
#endif
#ifndef __has_builtin
# define __has_builtin(x) 0  // Compatibility with non-clang compilers.
#endif
#ifndef __has_feature
# define __has_feature(x) 0  // Compatibility with non-clang compilers.
#endif
#ifndef __has_extension
# define __has_extension __has_feature // Compatibility with pre-3.0 compilers.
#endif
#ifndef __has_cpp_attribute
# define __has_cpp_attribute(x) 0
#endif

#ifdef _MSC_VER
# define EDUKE32_MSVC_PREREQ(major) ((major) <= (_MSC_VER))
# ifdef __cplusplus
#  define EDUKE32_MSVC_CXX_PREREQ(major) ((major) <= (_MSC_VER))
# else
#  define EDUKE32_MSVC_CXX_PREREQ(major) 0
# endif
#else
# define EDUKE32_MSVC_PREREQ(major) 0
# define EDUKE32_MSVC_CXX_PREREQ(major) 0
#endif


////////// Language detection //////////

#if defined __STDC__
# if defined __STDC_VERSION__ && __STDC_VERSION__ >= 201112L
#  define CSTD 2011
# elif defined __STDC_VERSION__ && __STDC_VERSION__ >= 199901L
#  define CSTD 1999
# elif defined __STDC_VERSION__ && __STDC_VERSION__ >= 199409L
#  define CSTD 1994
# else
#  define CSTD 1989
# endif
#else
# define CSTD 0
#endif

#if defined __cplusplus && __cplusplus >= 201703L
# define CXXSTD 2017
#elif defined __cplusplus && __cplusplus >= 201402L
# define CXXSTD 2014
#elif defined __cplusplus && __cplusplus >= 201103L
# define CXXSTD 2011
#elif defined __cplusplus && __cplusplus >= 199711L
# define CXXSTD 1998
#else
# define CXXSTD 0
#endif


////////// Language and compiler feature polyfills //////////

#ifdef __cplusplus
# define EXTERNC extern "C"
#else
# define EXTERNC
#endif

#ifndef UNREFERENCED_PARAMETER
# define UNREFERENCED_PARAMETER(x) (x) = (x)
#endif

#ifndef UNREFERENCED_CONST_PARAMETER
# ifdef _MSC_VER
#  define UNREFERENCED_CONST_PARAMETER(x) ((void)(x))
# else
#  define UNREFERENCED_CONST_PARAMETER(x)
# endif
#endif

#ifdef __GNUC__
# define UNUSED(x) UNUSED_ ## x __attribute__((unused))
# define PRINTF_FORMAT(stringindex, firstargindex) __attribute__((format (printf, stringindex, firstargindex)))
#else
# define UNUSED(x) x
# define PRINTF_FORMAT(stringindex, firstargindex)
#endif

#if defined __GNUC__ || defined __clang__
# define ATTRIBUTE(attrlist) __attribute__(attrlist)
#else
# define ATTRIBUTE(attrlist)
#endif

#if !defined __clang__ && !defined USING_LTO
# define ATTRIBUTE_OPTIMIZE(str) ATTRIBUTE((optimize(str)))
#else
# define ATTRIBUTE_OPTIMIZE(str)
#endif

#if EDUKE32_GCC_PREREQ(4,0)
# define WARN_UNUSED_RESULT __attribute__((warn_unused_result))
#else
# define WARN_UNUSED_RESULT
#endif

#if defined _MSC_VER && _MSC_VER < 1800
# define inline __inline
#endif

#ifndef MAY_ALIAS
# ifdef _MSC_VER
#  define MAY_ALIAS
# else
#  define MAY_ALIAS __attribute__((may_alias))
# endif
#endif

#ifndef FORCE_INLINE
# ifdef _MSC_VER
#  define FORCE_INLINE __forceinline
# else
#  ifdef __GNUC__
#    define FORCE_INLINE inline __attribute__((always_inline))
#  else
#    define FORCE_INLINE inline
#  endif
# endif
#endif

#ifndef _MSC_VER
#  ifndef __fastcall
#    if defined(__GNUC__) && defined(__i386__)
#      define __fastcall __attribute__((fastcall))
#    else
#      define __fastcall
#    endif
#  endif
#endif

#ifndef DISABLE_INLINING
# define EXTERN_INLINE static inline
# define EXTERN_INLINE_HEADER static inline
#else
# define EXTERN_INLINE __fastcall
# define EXTERN_INLINE_HEADER extern __fastcall
#endif

#if 0 && defined(__OPTIMIZE__) && (defined __GNUC__ || __has_builtin(__builtin_expect))
#define EDUKE32_PREDICT_TRUE(x)       __builtin_expect(!!(x),1)
#define EDUKE32_PREDICT_FALSE(x)     __builtin_expect(!!(x),0)
#else
#define EDUKE32_PREDICT_TRUE(x) (x)
#define EDUKE32_PREDICT_FALSE(x) (x)
#endif

#if EDUKE32_GCC_PREREQ(4,5)  || __has_builtin(__builtin_unreachable)
#define EDUKE32_UNREACHABLE_SECTION(...)   __builtin_unreachable()
#elif _MSC_VER
#define EDUKE32_UNREACHABLE_SECTION(...)   __assume(0)
#else
#define EDUKE32_UNREACHABLE_SECTION(...) __VA_ARGS__
#endif

#if EDUKE32_GCC_PREREQ(2,0) || defined _MSC_VER
# define EDUKE32_FUNCTION __FUNCTION__
#elif CSTD >= 1999 || CXXSTD >= 2011
# define EDUKE32_FUNCTION __func__
#else
# define EDUKE32_FUNCTION "???"
#endif

#ifdef _MSC_VER
# define EDUKE32_PRETTY_FUNCTION __FUNCSIG__
#elif EDUKE32_GCC_PREREQ(2,0)
# define EDUKE32_PRETTY_FUNCTION __PRETTY_FUNCTION__
#else
# define EDUKE32_PRETTY_FUNCTION EDUKE32_FUNCTION
#endif

#ifdef __COUNTER__
# define EDUKE32_UNIQUE_SRC_ID __COUNTER__
#else
# define EDUKE32_UNIQUE_SRC_ID __LINE__
#endif

#if CXXSTD >= 2017
# define EDUKE32_STATIC_ASSERT(cond) static_assert(cond)
#elif CXXSTD >= 2011 || CSTD >= 2011 || EDUKE32_MSVC_CXX_PREREQ(1600)
# define EDUKE32_STATIC_ASSERT(cond) static_assert(cond, "")
#else
/* C99 / C++03 static assertions based on source found in LuaJIT's src/lj_def.h. */
# define EDUKE32_ASSERT_NAME2(name, line) name ## line
# define EDUKE32_ASSERT_NAME(line) EDUKE32_ASSERT_NAME2(eduke32_assert_, line)
# define EDUKE32_STATIC_ASSERT(cond) \
    extern void EDUKE32_ASSERT_NAME(EDUKE32_UNIQUE_SRC_ID)(int STATIC_ASSERTION_FAILED[(cond)?1:-1])
#endif

#ifdef _MSC_VER
# define longlong(x) x##i64
#else
# define longlong(x) x##ll
#endif

#ifndef FP_OFF
# define FP_OFF(__p) ((uintptr_t)(__p))
#endif

#ifdef UNDERSCORES
# define ASMSYM(x) "_" x
#else
# define ASMSYM(x) x
#endif

#if defined __cplusplus
# define STATIC_CAST_OP(t) static_cast<t>
# define REINTERPRET_CAST_OP(t) reinterpret_cast<t>
#else
# define STATIC_CAST_OP(t) (t)
# define REINTERPRET_CAST_OP(t) (t)
#endif
#define STATIC_CAST(t, v) (STATIC_CAST_OP(t)(v))
#define REINTERPRET_CAST(t, v) (REINTERPRET_CAST_OP(t)(v))

#if defined __cplusplus && (__cplusplus >= 201103L || __has_feature(cxx_constexpr) || EDUKE32_MSVC_PREREQ(1900))
# define HAVE_CONSTEXPR
# define CONSTEXPR constexpr
#else
# define CONSTEXPR
#endif

#if CXXSTD >= 2011 || EDUKE32_MSVC_PREREQ(1700)
# define FINAL final
#else
# define FINAL
#endif

#if CXXSTD >= 2014
# define CONSTEXPR_CXX14 CONSTEXPR
#else
# define CONSTEXPR_CXX14
#endif

#if CXXSTD >= 2011
# if __has_cpp_attribute(fallthrough)
#  define fallthrough__ [[fallthrough]]
# elif __has_cpp_attribute(clang::fallthrough)
#  define fallthrough__ [[clang::fallthrough]]
# elif __has_cpp_attribute(gnu::fallthrough)
#  define fallthrough__ [[gnu::fallthrough]]
# endif
#endif
#ifndef fallthrough__
# if !defined __clang__ && EDUKE32_GCC_PREREQ(7,0)
#  define fallthrough__ __attribute__((fallthrough))
# elif defined _MSC_VER
#  define fallthrough__ __fallthrough
# else
#  define fallthrough__
# endif
#endif


////////// Platform detection //////////

#if defined __FreeBSD__ || defined __NetBSD__ || defined __OpenBSD__ || defined __bsdi__ || defined __DragonFly__
# define EDUKE32_BSD
#endif

#ifdef __APPLE__
# include <TargetConditionals.h>
# if TARGET_OS_IPHONE || TARGET_IPHONE_SIMULATOR
#  define EDUKE32_IOS
# else
#  define EDUKE32_OSX
# endif
#endif


////////// Architecture detection //////////

#if defined __arm__ || defined __aarch64__
# define EDUKE32_CPU_ARM
#elif defined __i386 || defined __i386__ || defined _M_IX86 || defined _M_X64 || defined __x86_64__
# define EDUKE32_CPU_X86
#elif defined _M_PPC || defined __powerpc__ || defined __powerpc64__
# define EDUKE32_CPU_PPC
#elif defined __MIPSEL__ || defined __mips_isa_rev
# define EDUKE32_CPU_MIPS
#endif

#if defined _LP64 || defined __LP64__ || defined __64BIT__ || _ADDR64 || defined _WIN64 || defined __arch64__ ||       \
__WORDSIZE == 64 || (defined __sparc && defined __sparcv9) || defined __x86_64 || defined __amd64 ||                   \
defined __x86_64__ || defined __amd64__ || defined _M_X64 || defined _M_IA64 || defined __ia64 || defined __IA64__

# define BITNESS64

#endif

#if defined(__linux)
# include <endian.h>
# if __BYTE_ORDER == __LITTLE_ENDIAN
#  define B_LITTLE_ENDIAN 1
#  define B_BIG_ENDIAN    0
# elif __BYTE_ORDER == __BIG_ENDIAN
#  define B_LITTLE_ENDIAN 0
#  define B_BIG_ENDIAN    1
# endif

#elif defined(GEKKO) || defined(__ANDROID__)
# define B_LITTLE_ENDIAN 0
# define B_BIG_ENDIAN 1

#elif defined(__OpenBSD__)
# include <machine/endian.h>
# if _BYTE_ORDER == _LITTLE_ENDIAN
#  define B_LITTLE_ENDIAN 1
#  define B_BIG_ENDIAN    0
# elif _BYTE_ORDER == _BIG_ENDIAN
#  define B_LITTLE_ENDIAN 0
#  define B_BIG_ENDIAN    1
# endif

#elif defined EDUKE32_BSD
# include <sys/endian.h>
# if _BYTE_ORDER == _LITTLE_ENDIAN
#  define B_LITTLE_ENDIAN 1
#  define B_BIG_ENDIAN    0
# elif _BYTE_ORDER == _BIG_ENDIAN
#  define B_LITTLE_ENDIAN 0
#  define B_BIG_ENDIAN    1
# endif

#elif defined(__APPLE__)
# if defined(__LITTLE_ENDIAN__)
#  define B_LITTLE_ENDIAN 1
#  define B_BIG_ENDIAN    0
# elif defined(__BIG_ENDIAN__)
#  define B_LITTLE_ENDIAN 0
#  define B_BIG_ENDIAN    1
# endif
# include <libkern/OSByteOrder.h>

#elif defined(__BEOS__)
# include <posix/endian.h>
# if LITTLE_ENDIAN != 0
#  define B_LITTLE_ENDIAN 1
#  define B_BIG_ENDIAN    0
# elif BIG_ENDIAN != 0
#  define B_LITTLE_ENDIAN 0
#  define B_BIG_ENDIAN    1
# endif

#elif defined(__QNX__)
# if defined __LITTLEENDIAN__
#  define B_LITTLE_ENDIAN 1
#  define B_BIG_ENDIAN    0
# elif defined __BIGENDIAN__
#  define B_LITTLE_ENDIAN 0
#  define B_BIG_ENDIAN    1
# endif

#elif defined(__sun)
# if defined _LITTLE_ENDIAN
#  define B_LITTLE_ENDIAN 1
#  define B_BIG_ENDIAN    0
# elif defined _BIG_ENDIAN
#  define B_LITTLE_ENDIAN 0
#  define B_BIG_ENDIAN    1
# endif

#elif defined(_WIN32) || defined(SKYOS) || defined(__SYLLABLE__)
# define B_LITTLE_ENDIAN 1
# define B_BIG_ENDIAN    0
#endif

#if !defined(B_LITTLE_ENDIAN) || !defined(B_BIG_ENDIAN)
# error Unknown endianness
#endif


////////// Standard library headers //////////

#undef __USE_MINGW_ANSI_STDIO // Workaround for MinGW-w64.

#ifndef __STDC_FORMAT_MACROS
# define __STDC_FORMAT_MACROS
#endif
#ifndef __STDC_LIMIT_MACROS
# define __STDC_LIMIT_MACROS
#endif

#ifndef _USE_MATH_DEFINES
# define _USE_MATH_DEFINES
#endif

#include <inttypes.h>
#include <stdint.h>

#include <limits.h>
#include <stdarg.h>
#include <stddef.h>
#ifndef USE_PHYSFS
#include <stdio.h>
#endif
#include <stdlib.h>
#include <string.h>

#if !(defined _WIN32 && defined __clang__)
#include <float.h>
#endif
#include <math.h>

#include <ctype.h>
#include <errno.h>
#include <time.h>

#include <assert.h>

#ifdef __cplusplus
# include <limits>
# if CXXSTD >= 2011 || EDUKE32_MSVC_PREREQ(1800)
#  include <algorithm>
#  include <functional>
#  include <type_traits>
// we need this because MSVC does not properly identify C++11 support
#  define HAVE_CXX11_HEADERS
# endif
#endif

////////// Platform headers //////////

#if !defined __APPLE__ && (!defined EDUKE32_BSD || !__STDC__)
# include <malloc.h>
#endif

#ifndef USE_PHYSFS
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#if defined(_WIN32)
# include <direct.h>
# include <io.h>
#else
# include <unistd.h>
#endif
#endif


////////// DEPRECATED: Standard library prefixing //////////

#ifdef _MSC_VER
# if defined _M_AMD64 || defined _M_ARM64 || defined _M_X64 || defined _WIN64
// should be int64_t, if not for a suspected VS compiler bug
typedef int32_t ssize_t;
# else
typedef int32_t ssize_t;
# endif
#endif

typedef size_t bsize_t;
typedef ssize_t bssize_t;

typedef FILE BFILE;

#define BO_BINARY O_BINARY
#define BO_TEXT   O_TEXT
#define BO_RDONLY O_RDONLY
#define BO_WRONLY O_WRONLY
#define BO_RDWR   O_RDWR
#define BO_APPEND O_APPEND
#define BO_CREAT  O_CREAT
#define BO_TRUNC  O_TRUNC
#define BS_IRGRP  S_IRGRP
#define BS_IWGRP  S_IWGRP
#define BS_IEXEC  S_IEXEC
#define BS_IFIFO  S_IFIFO
#define BS_IFCHR  S_IFCHR
#define BS_IFBLK  S_IFBLK
#define BS_IFDIR  S_IFDIR
#define BS_IFREG  S_IFREG
#define BSEEK_SET SEEK_SET
#define BSEEK_CUR SEEK_CUR
#define BSEEK_END SEEK_END

#define BMAX_PATH 256

#define Bassert assert
#define Brand rand
#define Bmalloc malloc
#define Bcalloc calloc
#define Brealloc realloc
#define Bfree free
#define Bopen open
#define Bclose close
#define Bwrite write
#define Bread read
#define Blseek lseek
#define Bstat stat
#define Bfstat fstat
#define Bfileno fileno
#define Bferror ferror
#define Bfopen fopen
#define Bfclose fclose
#define Bfflush fflush
#define Bfeof feof
#define Bfgetc fgetc
#define Brewind rewind
#define Bfgets fgets
#define Bfputc fputc
#define Bfputs fputs
#define Bfread fread
#define Bfwrite fwrite
#define Bfprintf fprintf
#define Bfscanf fscanf
#define Bfseek fseek
#define Bftell ftell
#define Bputs puts
#define Bstrcpy strcpy
#define Bstrncpy strncpy
#define Bstrcmp strcmp
#define Bstrncmp strncmp
#define Bstrcat strcat
#define Bstrncat strncat
#define Bstrlen strlen
#define Bstrchr strchr
#define Bstrrchr strrchr
#define Bstrtol strtol
#define Bstrtoul strtoul
#define Bstrtod strtod
#define Bstrstr strstr
#define Bislower islower
#define Bisupper isupper
#define Bisdigit isdigit
#define Btoupper toupper
#define Btolower tolower
#define Bmemcpy memcpy
#define Bmemmove memmove
#define Bmemchr memchr
#define Bmemset memset
#define Bmemcmp memcmp
#define Bscanf scanf
#define Bprintf printf
#define Bsscanf sscanf
#define Bsprintf sprintf
#define Bvfprintf vfprintf
#define Bgetenv getenv
#define Butime utime


////////// Standard library wrappers //////////

#ifdef __ANDROID__
# define BS_IWRITE S_IWUSR
# define BS_IREAD  S_IRUSR
#else
# define BS_IWRITE S_IWRITE
# define BS_IREAD  S_IREAD
#endif

#if defined(__cplusplus) && defined(_MSC_VER)
# define Bstrdup _strdup
# define Bchdir _chdir
# define Bgetcwd _getcwd
#else
# define Bstrdup strdup
# define Bchdir chdir
# define Bgetcwd getcwd
#endif

#if defined(__GNUC__)
# define Btell(h) lseek(h,0,SEEK_CUR)
#else
# define Btell tell
#endif

#if defined(_MSC_VER)
# define Bstrcasecmp _stricmp
# define Bstrncasecmp _strnicmp
#elif defined(__QNX__)
# define Bstrcasecmp stricmp
# define Bstrncasecmp strnicmp
#else
# define Bstrcasecmp strcasecmp
# define Bstrncasecmp strncasecmp
#endif

#ifdef _WIN32
# define Bsnprintf _snprintf
# define Bvsnprintf _vsnprintf
#else
# define Bsnprintf snprintf
# define Bvsnprintf vsnprintf
#endif

#ifdef _MSC_VER
# define Balloca _alloca
#elif defined __GNUC__
# define Balloca __builtin_alloca
#else
# define Balloca alloca
#endif

#ifdef _MSC_VER
# define Bmalloca _malloca
# define Bfreea _freea
#else
# define Bmalloca alloca
# define Bfreea(ptr) do { } while (0)
#endif

#define Btime() time(NULL)

#if defined(_WIN32)
# define Bmkdir(s,x) mkdir(s)
#else
# define Bmkdir mkdir
#endif

// XXX: different across 32- and 64-bit archs (e.g.
// parsing the decimal representation of 0xffffffff,
// 4294967295 -- long is signed, so strtol would
// return LONG_MAX (== 0x7fffffff on 32-bit archs))

static FORCE_INLINE int32_t atoi_safe(const char *str) { return (int32_t)Bstrtol(str, NULL, 10); }

#define Batoi(x) atoi_safe(x)
#define Batol(str) (strtol(str, NULL, 10))
#define Batof(str) (strtod(str, NULL))

#if defined BITNESS64 && (defined __SSE2__ || defined _MSC_VER)
#include <emmintrin.h>
static FORCE_INLINE int32_t Blrintf(const float x)
{
    __m128 xx = _mm_load_ss(&x);
    return _mm_cvtss_si32(xx);
}
#elif defined (_MSC_VER)
static FORCE_INLINE int32_t Blrintf(const float x)
{
    int n;
    __asm fld x;
    __asm fistp n;
    return n;
}
#else
#define Blrintf(x) ((int32_t)lrintf(x))
#endif

#if defined(__arm__)
# define Bsqrt __builtin_sqrt
# define Bsqrtf __builtin_sqrtf
#else
# define Bsqrt sqrt
# define Bsqrtf sqrtf
#endif

// redefined for apple/ppc, which chokes on stderr when linking...
#if defined EDUKE32_OSX && defined __BIG_ENDIAN__
# define ERRprintf(fmt, ...) printf(fmt, ## __VA_ARGS__)
#else
# define ERRprintf(fmt, ...) fprintf(stderr, fmt, ## __VA_ARGS__)
#endif

// Bexit is ONLY for errors!
#ifdef DEBUGGINGAIDS
# define Bexit(status) do { initprintf("exit(%d) at %s:%d in %s()\n", status, __FILE__, __LINE__, EDUKE32_FUNCTION); exit(status); } while (0)
#else
# define Bexit exit
#endif

#ifdef _WIN32
#define fatal_exit__(x) FatalAppExitA(0, x)
#else
#define fatal_exit__(x) do { wm_msgbox("Fatal Error", "%s", x); exit(EXIT_FAILURE); } while(0)
#endif

#define fatal_exit(status) do { initprintf("fatal_exit(%s) at %s:%d in %s()\n", status, __FILE__, __LINE__, EDUKE32_FUNCTION); fatal_exit__(status); } while (0)

////////// Standard library monkey patching //////////

#ifndef NULL
# define NULL ((void *)0)
#endif

#ifdef _MSC_VER
# define strtoll _strtoi64
#endif

#ifndef O_BINARY
# define O_BINARY 0
#endif
#ifndef O_TEXT
# define O_TEXT 0
#endif

#ifndef F_OK
# define F_OK 0
#endif

#ifdef GEKKO
# undef PRIdPTR
# define PRIdPTR "d"
# undef PRIxPTR
# define PRIxPTR "x"
# undef SCNx32
# define SCNx32 "x"
#endif

#if defined EDUKE32_OSX
# if !defined __x86_64__ && defined __GNUC__
// PK 20110617: is*() crashes for me in x86 code compiled from 64-bit, and gives link errors on ppc
//              This hack patches all occurences.
#  define isdigit(ch) ({ int32_t c__dontuse_=ch; c__dontuse_>='0' && c__dontuse_<='9'; })
#  define isalpha(ch) ({ int32_t c__dontuse2_=ch; (c__dontuse2_>='A' && c__dontuse2_<='Z') || (c__dontuse2_>='a' && c__dontuse2_<='z'); })
#  define isalnum(ch2)  ({ int32_t c2__dontuse_=ch2; isalpha(c2__dontuse_) || isdigit(c2__dontuse_); })
#  if defined __BIG_ENDIAN__
#   define isspace(ch)  ({ int32_t c__dontuse_=ch; (c__dontuse_==' ' || c__dontuse_=='\t' || c__dontuse_=='\n' || c__dontuse_=='\v' || c__dontuse_=='\f' || c__dontuse_=='\r'); })
#   define isprint(ch)  ({ int32_t c__dontuse_=ch; (c__dontuse_>=0x20 && c__dontuse_<0x7f); })
#  endif
# endif
#endif

#ifdef __ANDROID__
void eduke32_exit_return(int) ATTRIBUTE((noreturn));
# define exit(x) eduke32_exit_return(x)
#endif


////////// Metaprogramming structs //////////

#ifdef __cplusplus

# ifdef HAVE_CXX11_HEADERS
using std::is_integral;
template <typename T>
struct is_signed
{
    static constexpr bool value = std::is_signed<T>::value;
};
template <typename T>
struct is_unsigned
{
    static constexpr bool value = std::is_unsigned<T>::value;
};
# endif

# if CXXSTD >= 2014
using std::enable_if_t;
using std::conditional_t;
using std::make_signed_t;
using std::make_unsigned_t;
using std::remove_pointer_t;
# elif defined HAVE_CXX11_HEADERS
template <bool B, class T = void>
using enable_if_t = typename std::enable_if<B, T>::type;
template<bool B, class T, class F>
using conditional_t = typename std::conditional<B, T, F>::type;
template <typename T>
using make_signed_t = typename std::make_signed<T>::type;
template <typename T>
using make_unsigned_t = typename std::make_unsigned<T>::type;
template <class T>
using remove_pointer_t = typename std::remove_pointer<T>::type;
# endif

# ifdef HAVE_CXX11_HEADERS
template <typename type, typename other_type_with_sign>
using take_sign_t = conditional_t< is_signed<other_type_with_sign>::value, make_signed_t<type>, make_unsigned_t<type> >;
# endif

template <size_t size>
struct integers_of_size { };
template <>
struct integers_of_size<sizeof(int8_t)>
{
    typedef int8_t i;
    typedef uint8_t u;
};
template <>
struct integers_of_size<sizeof(int16_t)>
{
    typedef int16_t i;
    typedef uint16_t u;
};
template <>
struct integers_of_size<sizeof(int32_t)>
{
    typedef int32_t i;
    typedef uint32_t u;
};
template <>
struct integers_of_size<sizeof(int64_t)>
{
    typedef int64_t i;
    typedef uint64_t u;
};

#endif


////////// Typedefs //////////

#ifdef __cplusplus
// for use in SFINAE constructs in place of the pointer trick (to which 0 can unintentionally be implicitly cast)
struct Dummy FINAL
{
    FORCE_INLINE CONSTEXPR Dummy() : dummy(0) { }
    char dummy;
};
#endif

#if defined(__x86_64__)
// for 32-bit pointers in x86_64 code, such as `gcc -mx32`
typedef uint64_t reg_t;
typedef int64_t sreg_t;
#else
typedef size_t reg_t;
typedef ssize_t sreg_t;
#endif

#ifdef HAVE_CXX11_HEADERS
using  native_t = typename integers_of_size<sizeof(reg_t)>::i;
using unative_t = typename integers_of_size<sizeof(reg_t)>::u;
#else
typedef sreg_t native_t;
typedef reg_t unative_t;
#endif
EDUKE32_STATIC_ASSERT(sizeof(native_t) == sizeof(unative_t));

typedef struct MAY_ALIAS {
    int32_t x, y;
} vec2_t;

typedef struct MAY_ALIAS {
    int16_t x, y;
} vec2_16_t;

typedef struct {
    uint32_t x, y;
} vec2u_t;

typedef struct {
    float x, y;
} vec2f_t;

typedef struct {
    double x, y;
} vec2d_t;

typedef struct MAY_ALIAS {
    union {
        struct { int32_t x, y, z; };
        vec2_t  vec2;
    };
} vec3_t;

typedef struct MAY_ALIAS {
    union {
        struct { int16_t x, y, z; };
        vec2_16_t vec2;
    };
} vec3_16_t;

typedef struct {
    union {
        struct {
            union { float x, d; };
            union { float y, u; };
            union { float z, v; };
        };
        vec2f_t vec2;
    };
} vec3f_t;

EDUKE32_STATIC_ASSERT(sizeof(vec3f_t) == sizeof(float) * 3);

typedef struct {
    union { double x; double d; };
    union { double y; double u; };
    union { double z; double v; };
} vec3d_t;

EDUKE32_STATIC_ASSERT(sizeof(vec3d_t) == sizeof(double) * 3);

typedef struct {
    float x, y, z, w;
} vec4f_t;

typedef struct {
    float x, y, z, w;
} vec4d_t;


////////// Language tricks that depend on size_t //////////

#if defined _MSC_VER
# define ARRAY_SIZE(arr) _countof(arr)
#elif defined HAVE_CONSTEXPR
template <typename T, size_t N>
static FORCE_INLINE constexpr size_t ARRAY_SIZE(T const (&)[N]) noexcept
{
    return N;
}
#elif defined __cplusplus
struct bad_arg_to_ARRAY_SIZE
{
   class Is_pointer; // incomplete
   class Is_array {};
   template <typename T>
   static Is_pointer check_type(const T*, const T* const*);
   static Is_array check_type(const void*, const void*);
};
# define ARRAY_SIZE(arr) ( \
   0 * sizeof(reinterpret_cast<const ::bad_arg_to_ARRAY_SIZE*>(arr)) + \
   0 * sizeof(::bad_arg_to_ARRAY_SIZE::check_type((arr), &(arr))) + \
   sizeof(arr) / sizeof((arr)[0]) )
#else
# define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#endif
#define ARRAY_SSIZE(arr) (native_t)ARRAY_SIZE(arr)


////////// Memory management //////////

#if !defined NO_ALIGNED_MALLOC
static FORCE_INLINE void *Baligned_alloc(const size_t alignment, const size_t size)
{
#ifdef _WIN32
    void *ptr = _aligned_malloc(size, alignment);
#elif defined __APPLE__ || defined EDUKE32_BSD
    void *ptr = NULL;
    posix_memalign(&ptr, alignment, size);
#else
    void *ptr = memalign(alignment, size);
#endif

    return ptr;
}
#else
# define Baligned_alloc(alignment, size) Bmalloc(size)
#endif

#if defined _WIN32 && !defined NO_ALIGNED_MALLOC
# define Baligned_free _aligned_free
#else
# define Baligned_free Bfree
#endif


////////// Pointer management //////////

#define DO_FREE_AND_NULL(var) do { \
    Xfree(var); (var) = NULL; \
} while (0)

#define ALIGNED_FREE_AND_NULL(var) do { \
    Xaligned_free(var); (var) = NULL; \
} while (0)


////////// Data serialization //////////

static FORCE_INLINE CONSTEXPR uint16_t B_SWAP16_impl(uint16_t value)
{
    return
        ((value & 0xFF00u) >> 8u) |
        ((value & 0x00FFu) << 8u);
}
static FORCE_INLINE CONSTEXPR uint32_t B_SWAP32_impl(uint32_t value)
{
    return
        ((value & 0xFF000000u) >> 24u) |
        ((value & 0x00FF0000u) >>  8u) |
        ((value & 0x0000FF00u) <<  8u) |
        ((value & 0x000000FFu) << 24u);
}
static FORCE_INLINE CONSTEXPR uint64_t B_SWAP64_impl(uint64_t value)
{
    return
      ((value & 0xFF00000000000000ULL) >> 56ULL) |
      ((value & 0x00FF000000000000ULL) >> 40ULL) |
      ((value & 0x0000FF0000000000ULL) >> 24ULL) |
      ((value & 0x000000FF00000000ULL) >>  8ULL) |
      ((value & 0x00000000FF000000ULL) <<  8ULL) |
      ((value & 0x0000000000FF0000ULL) << 24ULL) |
      ((value & 0x000000000000FF00ULL) << 40ULL) |
      ((value & 0x00000000000000FFULL) << 56ULL);
}

/* The purpose of B_PASS* as functions, as opposed to macros, is to prevent them from being used as lvalues. */
#if CXXSTD >= 2011 || EDUKE32_MSVC_PREREQ(1900)
template <typename T>
static FORCE_INLINE CONSTEXPR take_sign_t<int16_t, T> B_SWAP16(T x)
{
    return static_cast< take_sign_t<int16_t, T> >(B_SWAP16_impl(static_cast<uint16_t>(x)));
}
template <typename T>
static FORCE_INLINE CONSTEXPR take_sign_t<int32_t, T> B_SWAP32(T x)
{
    return static_cast< take_sign_t<int32_t, T> >(B_SWAP32_impl(static_cast<uint32_t>(x)));
}
template <typename T>
static FORCE_INLINE CONSTEXPR take_sign_t<int64_t, T> B_SWAP64(T x)
{
    return static_cast< take_sign_t<int64_t, T> >(B_SWAP64_impl(static_cast<uint64_t>(x)));
}

template <typename T>
static FORCE_INLINE CONSTEXPR take_sign_t<int16_t, T> B_PASS16(T x)
{
    return static_cast< take_sign_t<int16_t, T> >(x);
}
template <typename T>
static FORCE_INLINE CONSTEXPR take_sign_t<int32_t, T> B_PASS32(T x)
{
    return static_cast< take_sign_t<int32_t, T> >(x);
}
template <typename T>
static FORCE_INLINE CONSTEXPR take_sign_t<int64_t, T> B_PASS64(T x)
{
    return static_cast< take_sign_t<int64_t, T> >(x);
}
#else
#define B_SWAP16(x) B_SWAP16_impl(x)
#define B_SWAP32(x) B_SWAP32_impl(x)
#define B_SWAP64(x) B_SWAP64_impl(x)

static FORCE_INLINE CONSTEXPR uint16_t B_PASS16(uint16_t const x) { return x; }
static FORCE_INLINE CONSTEXPR uint32_t B_PASS32(uint32_t const x) { return x; }
static FORCE_INLINE CONSTEXPR uint64_t B_PASS64(uint64_t const x) { return x; }
#endif

#if B_LITTLE_ENDIAN == 1
# define B_LITTLE64(x) B_PASS64(x)
# define B_BIG64(x)    B_SWAP64(x)
# define B_LITTLE32(x) B_PASS32(x)
# define B_BIG32(x)    B_SWAP32(x)
# define B_LITTLE16(x) B_PASS16(x)
# define B_BIG16(x)    B_SWAP16(x)
#elif B_BIG_ENDIAN == 1
# define B_LITTLE64(x) B_SWAP64(x)
# define B_BIG64(x)    B_PASS64(x)
# define B_LITTLE32(x) B_SWAP32(x)
# define B_BIG32(x)    B_PASS32(x)
# define B_LITTLE16(x) B_SWAP16(x)
# define B_BIG16(x)    B_PASS16(x)
#endif

// TODO: Determine when, if ever, we should use the bit-shift-and-mask variants
// due to alignment issues or performance gains.
#if 1
static FORCE_INLINE void B_BUF16(void * const buf, uint16_t const x) { *(uint16_t *) buf = x; }
static FORCE_INLINE void B_BUF32(void * const buf, uint32_t const x) { *(uint32_t *) buf = x; }
static FORCE_INLINE void B_BUF64(void * const buf, uint64_t const x) { *(uint64_t *) buf = x; }

static FORCE_INLINE CONSTEXPR uint16_t B_UNBUF16(void const * const buf) { return *(uint16_t const *) buf; }
static FORCE_INLINE CONSTEXPR uint32_t B_UNBUF32(void const * const buf) { return *(uint32_t const *) buf; }
static FORCE_INLINE CONSTEXPR uint64_t B_UNBUF64(void const * const buf) { return *(uint64_t const *) buf; }
#else
static FORCE_INLINE void B_BUF16(void * const vbuf, uint16_t const x)
{
    uint8_t * const buf = (uint8_t *) vbuf;
    buf[0] = (x & 0x00FF);
    buf[1] = (x & 0xFF00) >> 8;
}
static FORCE_INLINE void B_BUF32(void * const vbuf, uint32_t const x)
{
    uint8_t * const buf = (uint8_t *) vbuf;
    buf[0] = (x & 0x000000FF);
    buf[1] = (x & 0x0000FF00) >> 8;
    buf[2] = (x & 0x00FF0000) >> 16;
    buf[3] = (x & 0xFF000000) >> 24;
}
# if 0
// i686-apple-darwin11-llvm-gcc-4.2 complains "integer constant is too large for 'long' type"
static FORCE_INLINE void B_BUF64(void * const vbuf, uint64_t const x)
{
    uint8_t * const buf = (uint8_t *) vbuf;
    buf[0] = (x & 0x00000000000000FF);
    buf[1] = (x & 0x000000000000FF00) >> 8;
    buf[2] = (x & 0x0000000000FF0000) >> 16;
    buf[3] = (x & 0x00000000FF000000) >> 24;
    buf[4] = (x & 0x000000FF00000000) >> 32;
    buf[5] = (x & 0x0000FF0000000000) >> 40;
    buf[6] = (x & 0x00FF000000000000) >> 48;
    buf[7] = (x & 0xFF00000000000000) >> 56;
}
# endif

static FORCE_INLINE uint16_t B_UNBUF16(void const * const vbuf)
{
    uint8_t const * const buf = (uint8_t const *) vbuf;
    return (buf[1] << 8) | (buf[0]);
}
static FORCE_INLINE uint32_t B_UNBUF32(void const * const vbuf)
{
    uint8_t const * const buf = (uint8_t const *) vbuf;
    return (buf[3] << 24) | (buf[2] << 16) | (buf[1] << 8) | (buf[0]);
}
static FORCE_INLINE uint64_t B_UNBUF64(void const * const vbuf)
{
    uint8_t const * const buf = (uint8_t const *) vbuf;
    return ((uint64_t)buf[7] << 56) | ((uint64_t)buf[6] << 48) | ((uint64_t)buf[5] << 40) |
        ((uint64_t)buf[4] << 32) | (buf[3] << 24) | (buf[2] << 16) | (buf[1] << 8) | (buf[0]);
}
#endif


////////// Abstract data operations //////////

#define ABSTRACT_DECL static FORCE_INLINE WARN_UNUSED_RESULT CONSTEXPR

#ifdef __cplusplus
template <typename T, typename X, typename Y> ABSTRACT_DECL T clamp(T in, X min, Y max) { return in <= (T) min ? (T) min : (in >= (T) max ? (T) max : in); }
template <typename T, typename X, typename Y> ABSTRACT_DECL T clamp2(T in, X min, Y max) { return in >= (T) max ? (T) max : (in <= (T) min ? (T) min : in); }
using std::min;
using std::max;
# define fclamp clamp
# define fclamp2 clamp2
#else
// Clamp <in> to [<min>..<max>]. The case in <= min is handled first.
ABSTRACT_DECL int32_t clamp(int32_t in, int32_t min, int32_t max) { return in <= min ? min : (in >= max ? max : in); }
ABSTRACT_DECL float fclamp(float in, float min, float max) { return in <= min ? min : (in >= max ? max : in); }
// Clamp <in> to [<min>..<max>]. The case in >= max is handled first.
ABSTRACT_DECL int32_t clamp2(int32_t in, int32_t min, int32_t max) { return in >= max ? max : (in <= min ? min : in); }
ABSTRACT_DECL float fclamp2(float in, float min, float max) { return in >= max ? max : (in <= min ? min : in); }

#ifndef min
# define min(a, b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef max
# define max(a, b) (((a) > (b)) ? (a) : (b))
#endif
#endif

////////// Mathematical operations //////////

#ifdef __cplusplus
#ifdef HAVE_CXX11_HEADERS
template <typename T>
struct DivResult
{
    T q; // quotient
    T r; // remainder
};
template <typename T>
FORCE_INLINE CONSTEXPR DivResult<T> divide(T lhs, T rhs)
{
    return DivResult<T>{(T)(lhs / rhs), (T)(lhs % rhs)};
}
template <native_t base, typename T>
FORCE_INLINE CONSTEXPR DivResult<T> divrhs(T lhs)
{
    return divide(lhs, (T)base);
}

template <typename T, typename T2>
static FORCE_INLINE CONSTEXPR_CXX14 enable_if_t<is_signed<T>::value, T> NEGATE_ON_CONDITION(T value, T2 condition)
{
    T const invert = !!condition;
    return (value ^ -invert) + invert;
}
#endif

template <size_t base, typename T>
CONSTEXPR size_t logbase(T n)
{
    return n < static_cast<T>(base) ? 1 : 1 + logbase<base>(n / static_cast<T>(base));
}
// hackish version to work around the impossibility of representing abs(INT*_MIN)
template <size_t base, typename T>
CONSTEXPR size_t logbasenegative(T n)
{
    return n > static_cast<T>(-(native_t)base) ? 1 : 1 + logbase<base>(n / static_cast<T>(-(native_t)base));
}

#endif

#define isPow2OrZero(v) (((v) & ((v) - 1)) == 0)
#define isPow2(v) (isPow2OrZero(v) && (v))

////////// Bitfield manipulation //////////

static CONSTEXPR const char pow2char[8] = {1,2,4,8,16,32,64,128u};

static FORCE_INLINE void bitmap_set(uint8_t *const ptr, int const n) { ptr[n>>3] |= pow2char[n&7]; }
static FORCE_INLINE void bitmap_clear(uint8_t *const ptr, int const n) { ptr[n>>3] &= ~pow2char[n&7]; }
static FORCE_INLINE CONSTEXPR char bitmap_test(uint8_t const *const ptr, int const n) { return ptr[n>>3] & pow2char[n&7]; }

////////// Utility functions //////////

// breadth-first search helpers
template <typename T>
void bfirst_search_init(T *const list, uint8_t *const bitmap, T *const eltnumptr, int const maxelts, int const firstelt)
{
    Bmemset(bitmap, 0, (maxelts+7)>>3);

    list[0] = firstelt;
    bitmap_set(bitmap, firstelt);
    *eltnumptr = 1;
}

template <typename T>
void bfirst_search_try(T *const list, uint8_t *const bitmap, T *const eltnumptr, int const elt)
{
    if (!bitmap_test(bitmap, elt))
    {
        bitmap_set(bitmap, elt);
        list[(*eltnumptr)++] = elt;
    }
}

#if RAND_MAX == 32767
static FORCE_INLINE uint16_t system_15bit_rand(void) { return (uint16_t)rand(); }
#else  // RAND_MAX > 32767, assumed to be of the form 2^k - 1
static FORCE_INLINE uint16_t system_15bit_rand(void) { return ((uint16_t)rand())&0x7fff; }
#endif

// Copy min(strlen(src)+1, n) characters into dst, always terminate with a NUL.
static FORCE_INLINE char *Bstrncpyz(char *dst, const char *src, bsize_t n)
{
    Bstrncpy(dst, src, n);
    dst[n-1] = 0;
    return dst;
}

// Append extension when <outbuf> contains no dot.
// <ext> can be like ".mhk" or like "_crash.map", no need to start with a dot.
// The ugly name is deliberate: we should be checking the sizes of all buffers!
static inline void append_ext_UNSAFE(char *outbuf, const char *ext)
{
    char *p = Bstrrchr(outbuf,'.');

    if (!p)
        Bstrcat(outbuf, ext);
    else
        Bstrcpy(p, ext);
}

/* Begin dependence on compat.o object. */


#ifdef __cplusplus
extern "C" {
#endif


#ifndef USE_PHYSFS
////////// Directory enumeration //////////

struct Bdirent
{
    char *name;
    uint32_t mode;
    uint32_t size;
    uint32_t mtime;
    uint16_t namlen;
};

typedef void BDIR;

BDIR *Bopendir(const char *name);
struct Bdirent *Breaddir(BDIR *dir);
int32_t Bclosedir(BDIR *dir);
#endif


////////// Paths //////////

char *Bgethomedir(void);
char *Bgetappdir(void);

int32_t Bcorrectfilename(char *filename, int32_t removefn);
int32_t Bcanonicalisefilename(char *filename, int32_t removefn);

char *Bgetsystemdrives(void);


////////// String manipulation //////////

char *Bstrtoken(char *s, const char *delim, char **ptrptr, int chop);
char *Bstrtolower(char *str);

#define Bwildmatch wildmatch

#ifdef _WIN32
# ifdef _MSC_VER
#  define Bstrlwr _strlwr
#  define Bstrupr _strupr
# else
#  define Bstrlwr strlwr
#  define Bstrupr strupr
# endif
#else
char *Bstrlwr(char *);
char *Bstrupr(char *);
#endif

////////// Miscellaneous //////////

int Bgetpagesize(void);
size_t Bgetsysmemsize(void);

////////// PANICKING ALLOCATION WRAPPERS //////////

#ifdef DEBUGGINGAIDS
extern void xalloc_set_location(int32_t line, const char *file, const char *func);
#endif
void set_memerr_handler(void (*handlerfunc)(int32_t, const char *, const char *));
void *handle_memerr(void *);

static FORCE_INLINE char *xstrdup(const char *s)
{
    char *ptr = Bstrdup(s);
    return (EDUKE32_PREDICT_TRUE(ptr != NULL)) ? ptr : (char *)handle_memerr(ptr);
}

static FORCE_INLINE void *xmalloc(const bsize_t size)
{
    void *ptr = Bmalloc(size);
    return (EDUKE32_PREDICT_TRUE(ptr != NULL)) ? ptr : handle_memerr(ptr);
}

static FORCE_INLINE void *xcalloc(const bsize_t nmemb, const bsize_t size)
{
    void *ptr = Bcalloc(nmemb, size);
    return (EDUKE32_PREDICT_TRUE(ptr != NULL)) ? ptr : handle_memerr(ptr);
}

static FORCE_INLINE void *xrealloc(void * const ptr, const bsize_t size)
{
    void *newptr = Brealloc(ptr, size);

    // According to the C Standard,
    //  - ptr == NULL makes realloc() behave like malloc()
    //  - size == 0 make it behave like free() if ptr != NULL
    // Since we want to catch an out-of-mem in the first case, this leaves:
    return (EDUKE32_PREDICT_TRUE(newptr != NULL || size == 0)) ? newptr: handle_memerr(ptr);
}

#if !defined NO_ALIGNED_MALLOC
static FORCE_INLINE void *xaligned_alloc(const bsize_t alignment, const bsize_t size)
{
    void *ptr = Baligned_alloc(alignment, size);
    return (EDUKE32_PREDICT_TRUE(ptr != NULL)) ? ptr : handle_memerr(ptr);
}

static FORCE_INLINE void *xaligned_calloc(const bsize_t alignment, const bsize_t count, const bsize_t size)
{
    bsize_t const blocksize = count * size;
    void *ptr = Baligned_alloc(alignment, blocksize);
    if (EDUKE32_PREDICT_TRUE(ptr != NULL))
    {
        Bmemset(ptr, 0, blocksize);
        return ptr;
    }
    return handle_memerr(ptr);
}
#else
# define xaligned_alloc(alignment, size) xmalloc(size)
# define xaligned_calloc(alignment, count, size) xcalloc(count, size)
#endif

#ifdef DEBUGGINGAIDS
# define EDUKE32_PRE_XALLOC xalloc_set_location(__LINE__, __FILE__, EDUKE32_FUNCTION),
#else
# define EDUKE32_PRE_XALLOC
#endif

#define Xstrdup(s)    (EDUKE32_PRE_XALLOC xstrdup(s))
#define Xmalloc(size) (EDUKE32_PRE_XALLOC xmalloc(size))
#define Xcalloc(nmemb, size) (EDUKE32_PRE_XALLOC xcalloc(nmemb, size))
#define Xrealloc(ptr, size)  (EDUKE32_PRE_XALLOC xrealloc(ptr, size))
#define Xaligned_alloc(alignment, size) (EDUKE32_PRE_XALLOC xaligned_alloc(alignment, size))
#define Xaligned_calloc(alignment, count, size) (EDUKE32_PRE_XALLOC xaligned_calloc(alignment, count, size))
#define Xfree(ptr) (Bfree(ptr))
#define Xaligned_free(ptr) (Baligned_free(ptr))

#ifdef __cplusplus
}
#endif


////////// More utility functions //////////

static inline void maybe_grow_buffer(char ** const buffer, int32_t * const buffersize, int32_t const newsize)
{
    if (newsize > *buffersize)
    {
        *buffer = (char *)Xrealloc(*buffer, newsize);
        *buffersize = newsize;
    }
}


////////// Inlined external libraries //////////

#ifndef LIBDIVIDE_BODY
# define LIBDIVIDE_HEADER_ONLY
#endif
#define LIBDIVIDE_C_HEADERS
#define LIBDIVIDE_NONAMESPACE
#define LIBDIVIDE_NOINLINE
#include "fix16.h"
#include "libdivide.h"
#include "clockticks.hpp"
#include "debugbreak.h"

#include "zpl.h"

/* End dependence on compat.o object. */


////////// EDuke32-specific features //////////

#ifndef TRUE
# define TRUE 1
#endif

#ifndef FALSE
# define FALSE 0
#endif

#define WITHKPLIB

#if defined __ANDROID__ || defined EDUKE32_IOS
# define EDUKE32_TOUCH_DEVICES
# define EDUKE32_GLES
#endif

#if DEBUGGINGAIDS>=2
# define DEBUG_MAIN_ARRAYS
#endif

#if !defined DEBUG_MAIN_ARRAYS
# define HAVE_CLIPSHAPE_FEATURE
#endif

#endif // compat_h_
