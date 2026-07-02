#ifndef _RE_DEFINE_H
#define _RE_DEFINE_H

/* bool */
#ifndef TRUE
#define TRUE    1
#endif // TRUE

#ifndef FALSE
#define FALSE   0
#endif // FALSE



#if defined(__CC_ARM) /* arm compiler <__CLANG_ARM>*/

    #include <stdio.h>
    #include <string.h>

    typedef long            ssize_t;
    typedef signed int      int32_t;
    typedef unsigned int    uint32_t;
    typedef signed short    int16_t;
    typedef unsigned short  uint16_t;
    typedef signed char     int8_t;
    typedef unsigned char   uint8_t;
    typedef signed long long int64_t;
    typedef unsigned long long uint64_t;

    #define XJ_SECTION(x)               __attribute__((section(x)))
    #define XJ_UNUSED                   __attribute__((unused))
    #define XJ_USED                     __attribute__((used))
    #define XJ_ALIGN(n)                 __attribute__((aligned(n)))
    #define XJ_WEAK                     __attribute__((weak))
    #define XJ_INLINE                   static __inline

#elif defined(__ICCARM__)   /* iar compiler <__IAR_SYSTEMS_ICC__>*/

    #include <stdio.h>
    #include <string.h>

    typedef long            ssize_t;
    typedef signed int      int32_t;
    typedef unsigned int    uint32_t;
    typedef signed short    int16_t;
    typedef unsigned short  uint16_t;
    typedef signed char     int8_t;
    typedef unsigned char   uint8_t;
    typedef signed long long int64_t;
    typedef unsigned long long uint64_t;

    #define XJ_SECTION(x)               @ x
    #define XJ_UNUSED
    #define XJ_USED                     __root
    #define XJ_PRAGMA(x)                _Pragma(#x)
    #define XJ_ALIGN(n)                 PRAGMA(data_alignment=n)
    #define XJ_WEAK                     __weak
    #define XJ_INLINE                   static inline

#elif defined(__GNUC__) /* gcc compiler */

    #include <stdint.h>
    #include <stdio.h>
    #include <string.h>
    #include <stdlib.h>
    #include <math.h>

    #define XJ_SECTION(x)                 __attribute__((section(x)))
    #define XJ_UNUSED                     __attribute__((unused))
    #define XJ_USED                       __attribute__((used))
    #define XJ_ALIGN(n)                   __attribute__((aligned(n)))
    #define XJ_WEAK                       __attribute__((weak))
    #define XJ_INLINE                     static inline

#endif // defined

/* array size */
#define ARRAY_SIZE( _ARRAY_ ) (sizeof (_ARRAY_) / sizeof (_ARRAY_[0]))

/* conversion */
#define BigLittleSwap16(A)        ((((uint16_t)(A) & 0xff00) >> 8) | \
                                                       (((uint16_t)(A) & 0x00ff) << 8))

#define BigLittleSwap32(A)        ((((uint32_t)(A) & 0xff000000) >> 24) | \
                                                       (((uint32_t)(A) & 0x00ff0000) >> 8) | \
                                                       (((uint32_t)(A) & 0x0000ff00) << 8) | \
                                                       (((uint32_t)(A) & 0x000000ff) << 24))

/* get offset */
#define _OFFSETOF(TYPE, MEMBER)     ((long)(&(((TYPE *)0)->MEMBER)))


#endif /* !RE_DEFINE_H*/
