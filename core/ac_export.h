
#ifndef AC_EXPORT_H
#define AC_EXPORT_H

#ifdef AC_STATIC_DEFINE
#  define AC_EXPORT
#  define AC_NO_EXPORT
#else
#  ifndef AC_EXPORT
#    ifdef Anime4KCPPCore_EXPORTS
        /* We are building this library */
#      define AC_EXPORT 
#    else
        /* We are using this library */
#      define AC_EXPORT 
#    endif
#  endif

#  ifndef AC_NO_EXPORT
#    define AC_NO_EXPORT 
#  endif
#endif

#ifndef AC_DEPRECATED
#  define AC_DEPRECATED __declspec(deprecated)
#endif

#ifndef AC_DEPRECATED_EXPORT
#  define AC_DEPRECATED_EXPORT AC_EXPORT AC_DEPRECATED
#endif

#ifndef AC_DEPRECATED_NO_EXPORT
#  define AC_DEPRECATED_NO_EXPORT AC_NO_EXPORT AC_DEPRECATED
#endif

#if 0 /* DEFINE_NO_DEPRECATED */
#  ifndef AC_NO_DEPRECATED
#    define AC_NO_DEPRECATED
#  endif
#endif

#endif /* AC_EXPORT_H */
