#ifdef DEBUG
#define LOG(string, ...) printf(string, ## __VA_ARGS__)
#else
#define LOG(string, ...)
#endif
