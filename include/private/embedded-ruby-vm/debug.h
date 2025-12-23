// Debug logging macro - can be disabled by defining NDEBUG
#ifdef NDEBUG
    #define DEBUG_LOG(fmt, ...) ((void)0)
#else
    #define DEBUG_LOG(fmt, ...) do { \
        fprintf(stderr, "[DEBUG] " fmt "\n", ##__VA_ARGS__); \
        fflush(stderr); \
    } while(0)
#endif