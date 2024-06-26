#include "timeout.hpp"

#include <ctime>
#include <stdexcept>

/* Constructs a timeout starting from the current time */
Timeout::Timeout(uint64_t duration)
    : _duration(duration)
{
    reset();
}

/* Resets the timeout's starting time to the current time */
void Timeout::reset()
{
    _startTime = getCurrentTime();
    _isStopped = false;
}

/* Gets whether the timeout has expired or not */
bool Timeout::isExpired()
{
    if (_isStopped)
        return false;
    uint64_t currentTime = getCurrentTime();
    if (currentTime < _startTime)
        throw std::runtime_error("Time moved backwards; did the system time change?");
    return currentTime - _startTime >= _duration;
}

/* Stops the timeout so it can never expire */
void Timeout::stop()
{
    _isStopped = true;
}

/* Queries the current time from the operating system */
uint64_t Timeout::getCurrentTime()
{
#ifdef __42_LIKES_WASTING_CPU_CYCLES__
    // There is neither an C function allowed by subject nor a C++98
    // standard function to obtain the current millisecond time
    return static_cast<uint64_t>(std::time(NULL)) * 1000;
#else
    timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC_RAW, &ts) != 0)
        throw std::runtime_error("Unable to query monotonic system time");

    return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
#endif // __42_LIKES_WASTING_CPU_CYCLES__
}
