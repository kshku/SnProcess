#pragma once

#include <sncore/api_common.h>

#if defined(SN_PROCESS_STATIC)
    #define SN_PROCESS_API
#elif defined(SN_EXPORT)
    #define SN_PROCESS_API SN_API_HELPER_EXPORT
#else
    #define SN_PROCESS_API SN_API_HELPER_IMPORT
#endif
