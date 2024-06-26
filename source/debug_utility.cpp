#include "slice.hpp"
#include "config.hpp"
#include "debug_utility.hpp"

#include <ctype.h>
#include <iostream>

/* Prints the lowest 4 bits of the given value as a hexadecimal character */
static void printHexChar(unsigned char value)
{
    value &= 15;
    if (value >= 10)
        std::cout << static_cast<char>(value - 10 + 'a');
    else
        std::cout << static_cast<char>(value + '0');
}

/* Prints a string; escapes non-printable characters and quotes */
static void printString(Slice value)
{
    std::cout << '"';
    for (size_t index = 0; index < value.getLength(); index++)
    {
        char current = value[index];

        // String delimiter (eg. " displayed as \")
        if (current == '"')
            std::cout << "\\\"";
        // Printable character
        else if (std::isprint(current))
            std::cout << current;
        // Non-printable character (eg. ASCII NUL displayed as \x00)
        else
        {
            unsigned char currentUnsigned = static_cast<unsigned char>(current);
            std::cout << "\\x";
            printHexChar(currentUnsigned >> 4);
            printHexChar(currentUnsigned);
        }
    }
    std::cout << '"';
}

/* Prints a field of any string type; escapes non-printable characters and quotes */
static void printStringField(const char *prefix, Slice value)
{
    std::cout << prefix;
    printString(value);
    std::cout << std::endl;
}

/* Prints a field of boolean type */
static void printBoolField(const char *prefix, bool value)
{
    std::cout << prefix;
    if (value)
        std::cout << "yes";
    else
        std::cout << "no";
    std::cout << std::endl;
}

/* Prints a vector of strings*/

static void printStringVector(const char *prefix, const std::vector<std::string> &value)
{
    std::cout << prefix;
    for (size_t index = 0; index < value.size(); index++)
    {
        std::cout << value[index];
        if (index + 1 < value.size())
            std::cout << ", ";
    }
    std::cout << std::endl;
}

/* Prints a field of a human-readable IPv4 address and port */
static void printAddressField(const char *prefix, uint32_t host, uint16_t port)
{
    uint32_t ip[4];

    // Split the host into its components
    for (size_t index = 0; index < 4; index++)
        ip[index] = (host >> (24 + index * 8)) & 255;

    // Print the address
    std::cout << prefix
              << ip[0] << '.' << ip[1] << '.' << ip[2] << '.' << ip[3] << ':'
              << port << std::endl;
}

/* Prints a set of allowed HTTP methods */
static void printAllowedMethods(const std::set<HttpMethod> &methods)
{
    std::cout << "    Allowed methods: {";
    std::set<HttpMethod>::iterator method = methods.begin();
    while (method != methods.end())
    {
        std::cout << httpMethodToString(*method);
        method++;
        if (method == methods.end())
            break;
        std::cout << ", ";
    }
    std::cout << '}' << std::endl;
}

/* Prints the given configuration to standard output */
void Debug::printConfig(const ApplicationConfig &config)
{
    for (size_t index = 0; index < config.servers.size(); index++)
    {
        const ServerConfig &serverConfig = config.servers[index];

        // Print simple fields
        printStringVector("+ Server names: ", serverConfig.name);
        printAddressField("  Bind address: ", serverConfig.host, serverConfig.port);
        std::cout << "  Maximum allowed body size: " << serverConfig.maxBodySize << std::endl;

        // Print error pages
        std::map<int, std::string>::const_iterator errorPage = serverConfig.errorPages.begin();
        for (; errorPage != serverConfig.errorPages.end(); errorPage++)
        {
            std::cout << "  Error page for HTTP " << errorPage->first;
            printStringField(": ", errorPage->second);
        }

        // Print local routes
        for (size_t index = 0; index < serverConfig.localRoutes.size(); index++)
        {
            const LocalRouteConfig &routeConfig = serverConfig.localRoutes[index];

            // Print simple fields
            printStringField("  + Local route on ", routeConfig.path);
            printAllowedMethods(routeConfig.allowedMethods);
            printStringField("    Root directory: ", routeConfig.rootDirectory);
            printStringField("    Upload directory: ", routeConfig.uploadDirectory);
            printBoolField("    Uploads allowed?: ", routeConfig.allowUpload);
            printBoolField("    Listing allowed?: ", routeConfig.allowListing);

            // Print CGI types
            std::map<std::string, std::string>::const_iterator cgiType = routeConfig.cgiTypes.begin();
            for (; cgiType != routeConfig.cgiTypes.end(); cgiType++)
            {
                std::cout << "    Execute *." << cgiType->first
                          << " using " << cgiType->second << std::endl;
            }
        }

        // Print redirect routes
        for (size_t index = 0; index < serverConfig.redirectRoutes.size(); index++)
        {
            const RedirectRouteConfig &routeConfig = serverConfig.redirectRoutes[index];

            // Print simple fields
            printStringField("  + Redirect route on ", routeConfig.path);
            printAllowedMethods(routeConfig.allowedMethods);
            printStringField("    Redirect location: ", routeConfig.redirectLocation);
        }
    }
}

/* Prints the given HTTP request to standard output */
void Debug::printRequest(const HttpRequest &request)
{
    std::cout << "+ HTTP request" << std::endl;
    std::cout << "  Method: HTTP_METHOD_" << httpMethodToString(request.method) << std::endl;
    printStringField("  Query: ", request.query);
    printStringField("  Query path: ", request.queryPath);
    printStringField("  Query parameters: ", request.queryParameters);
    printBoolField("  Is legacy?: ", request.isLegacy);
    for (size_t index = 0; index < request.headers.size(); index++)
    {
        std::cout << "    Header ";
        printString(request.headers[index].getKey());
        std::cout << ": ";
        printString(request.headers[index].getValue());
        std::cout << std::endl;
    }
    std::cout << "  <Body of " << request.body.size() << " bytes>" << std::endl;
}
