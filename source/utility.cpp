#include "utility.hpp"

#include <errno.h>
#include <unistd.h>
#include <iostream>
#include <stdexcept>
#include <sys/stat.h>
#include <cstring>
#include <netdb.h>

/* Finds the substring `needle` in the given string `haystack` */
const void *Utility::find(const void *haystack, size_t haystackLength, const void *needle, size_t needleLength)
{
#ifdef __42_LIKES_WASTING_CPU_CYCLES__
    if (haystackLength == 0 || haystackLength < needleLength)
        return NULL;

    const char *current = reinterpret_cast<const char *>(haystack);
    const char *end = &current[haystackLength - needleLength + 1];
    for (; current < end; current++)
    {
        if (std::memcmp(current, needle, needleLength) == 0)
            return current;
    }

    return NULL;
#else
    return memmem(haystack, haystackLength, needle, needleLength);
#endif // __42_LIKES_WASTING_CPU_CYCLES__
}

/* Finds the character `needle` in the given string `haystack` */
const void *Utility::findReverse(const void *haystack, char needle, size_t haystackLength)
{
#ifdef __42_LIKES_WASTING_CPU_CYCLES__
    if (haystackLength == 0)
        return NULL;

    const char *current = &reinterpret_cast<const char *>(haystack)[haystackLength - 1];
    for (size_t index = 0; index < haystackLength; index++)
    {
        if (*current == needle)
            return current;
        current--;
    }

    return NULL;
#else
    return memrchr(haystack, needle, haystackLength);
#endif // __42_LIKES_WASTING_CPU_CYCLES__
}

/* Queries the type of node at the given FS path */
NodeType Utility::queryNodeType(const char *path)
{
    struct stat result;

    if (stat(path, &result) != 0)
    {
        if (errno == EACCES)
            return NODE_TYPE_NO_ACCESS;
        if (errno == ENOENT)
            return NODE_TYPE_NOT_FOUND;
        throw std::runtime_error("Unable to query file information");
    }

    // Separately check for permissions on the node since stat() doesn't account for this
    if (access(path, R_OK) != 0)
        return NODE_TYPE_NO_ACCESS;

    if (S_ISREG(result.st_mode))
        return NODE_TYPE_REGULAR;
    if (S_ISDIR(result.st_mode))
        return NODE_TYPE_DIRECTORY;
    return NODE_TYPE_UNSUPPORTED;
}

/* Checks whether the given path exceeds the root path, returns true if it doesn't */
bool Utility::checkPathLevel(Slice path)
{
    Slice  node;
    size_t level = 0;
    bool   isLastNode = false;

    // Get rid of leading slashes
    path.stripStart('/');

    while (!isLastNode)
    {
        // Treat the trailing part as another node
        if (!path.splitStart('/', node))
        {
            node = path;
            isLastNode = true;
        }
        // Don't allow the path to escape the root
        if (node == C_SLICE(".."))
        {
            if (level == 0)
                return false;
            level--;
        }
        // All nodes except '.' or empty ones increase the level
        else if (!node.isEmpty() && node != C_SLICE("."))
            level++;
    }

    return true;
}

/* Converts a uint32_t ipv4 adress to a string*/
std::string Utility::ipv4ToString(uint32_t ipv4Addr)
{
    char addrBuffer[16];
    std::sprintf(addrBuffer, "%u.%u.%u.%u", (ipv4Addr >> 24) & 0xFF, (ipv4Addr >> 16) & 0xFF,
        (ipv4Addr >> 8) & 0xFF, ipv4Addr & 0xFF);
    return std::string(addrBuffer);
}

/* Attempts to convert a string slice to a `size_t` */
bool Utility::parseSize(Slice string, size_t &outResult)
{
    char   current;
    size_t digit;
    size_t result = 0;

    // Catch empty strings
    if (string.isEmpty())
        return false;

    for (size_t index = 0; index < string.getLength(); index++)
    {
        // Check if the character is a digit
        current = string[index];
        if (current < '0' || current > '9')
            return false;

        // Shift the result to the left (decimal) and check overflow
        if (result != 0 && SIZE_MAX / result < 10)
            return false;
        result *= 10;

        // Add the digit and check overflow
        digit = static_cast<unsigned char>(current - '0');
        if (SIZE_MAX - result < digit)
            return false;
        result += digit;
    }

    outResult = result;
    return true;
}

/* Attempts to convert a hexadecimal character (0-9, A-F, a-f) into its numeric value (0-15) */
bool Utility::parseHexChar(char character, uint8_t &outResult)
{
    if (character >= '0' && character <= '9')
        outResult = static_cast<unsigned char>(character - '0');
    else if (character >= 'A' && character <= 'F')
        outResult = static_cast<unsigned char>(character - 'A' + 10);
    else if (character >= 'a' && character <= 'f')
        outResult = static_cast<unsigned char>(character - 'a' + 10);
    else
        return false;
    return true;
}

/* Attempts to convert a string slice to a `size_t` */
bool Utility::parseSizeHex(Slice string, size_t &outResult)
{
    uint8_t digit;
    size_t  result = 0;

    // Catch empty strings
    if (string.isEmpty())
        return false;

    for (size_t index = 0; index < string.getLength(); index++)
    {
        // Check if the character is a hex digit and transform it
        if (!parseHexChar(string[index], digit))
            return false;

        // Shift the result to the left and check overflow
        if (index >= sizeof(size_t) * 2)
            return false;
        result = (result << 4) | static_cast<size_t>(digit);
    }

    outResult = result;
    return true;
}

/* Attempts to convert a URL-encoded string slice to a URL-decoded string */
bool Utility::decodeUrl(Slice string, std::string &outResult)
{
    std::stringstream stream;
    uint8_t           upperFour, lowerFour;

    for (size_t index = 0; index < string.getLength(); index++)
    {
        // Handle special characters
        if (string[index] == '%')
        {
            // Check if there are enough characters left (2 hex digits after '%')
            if (string.getLength() - index < 3)
                return false;

            // Parse the two hex digits
            if (!parseHexChar(string[index + 1], upperFour))
                return false;
            if (!parseHexChar(string[index + 2], lowerFour))
                return false;

            // Combine the digits and write it to the string as a character
            stream << static_cast<char>((upperFour << 4) | lowerFour);
            index += 2; // The last character will be consumed by the loop
        }
        // Handle the '+' character as a space
        else if (string[index] == '+')
            stream << ' ';
        else
            stream << string[index];
    }

    outResult = stream.str();
    return true;
}
