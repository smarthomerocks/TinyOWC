#ifndef TinyOWC_h
#define TinyOWC_h

// Tag loggmessages with application namn
static const char* TAG = "TinyOWC";

/**
 * Gets seconds since Unix epoctime (1970-01-01)
 */
int64_t getEpocTime() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (tv.tv_sec * 1000LL + (tv.tv_usec / 1000LL));
}

#endif