#include "cgi_process.hpp"
#include "http_client.hpp"
#include "slice.hpp"
#include "application.hpp"
#include "signal_manager.hpp"

#include <cstring>
#include <stdlib.h>
#include <limits.h>
#include <unistd.h>

CgiPathInfo::CgiPathInfo(const std::string &nodePath)
{
    // Split script file and directory
    Slice scriptFileSlice;
    Slice scriptDirectorySlice(nodePath);
    if (!scriptDirectorySlice.splitEnd('/', scriptFileSlice))
        throw std::runtime_error("Unable to split script directory");

    // Populate the structure
    fileName = scriptFileSlice.toString();
    workingDirectory = scriptDirectorySlice.toString();
}

/* Constructs a CGI process from the given client, request and route result */
CgiProcess::CgiProcess(HttpClient *client, const HttpRequest &request, const RoutingInfo &routingInfo)
    : _state(CGI_PROCESS_RUNNING)
    , _pathInfo(routingInfo.nodePath)
    , _client(client)
    , _request(request)
    , _process(setupArguments(request, routingInfo, _pathInfo.fileName),
               setupEnvironment(request, routingInfo),
               _pathInfo.workingDirectory)
    , _timeout(TIMEOUT_CGI_MS)
    , _bodyOffset(0)
    , _subscribeFlags(0)
{
}

/* Destroys the process */
CgiProcess::~CgiProcess()
{
    if (_subscribeFlags & SUBSCRIBE_FLAG_INPUT && _process.getInputFileno() >= 0)
        _client->_application._dispatcher.unsubscribe(_process.getInputFileno());
    if (_subscribeFlags & SUBSCRIBE_FLAG_OUTPUT && _process.getOutputFileno() >= 0)
        _client->_application._dispatcher.unsubscribe(_process.getOutputFileno());
}

/* Handles one or multiple events */
void CgiProcess::handleEvents(uint32_t eventMask)
{
    if (_state != CGI_PROCESS_RUNNING)
        return;

    switch (_process.getStatus())
    {
        case PROCESS_RUNNING:
            if (eventMask & EPOLLOUT)
            {
                if (_bodyOffset < _request.body.size())
                {
                    // Write the request body to the process' standard input pipe
                    ssize_t result = write(_process.getInputFileno(), &_request.body[_bodyOffset], _request.body.size() - _bodyOffset);
                    if (result < 0)
                    {
                        if (SignalManager::shouldQuit())
                            return;
                        throw std::runtime_error("Unable to write to CGI process");
                    }
                    if (result == 0)
                        throw std::runtime_error("Unexpected end of stream");
                    _bodyOffset += static_cast<size_t>(result);
                }

                // If the whole body was written, close the input pipe and switch into output phase
                if (_bodyOffset >= _request.body.size())
                {
                    _client->_application._dispatcher.unsubscribe(_process.getInputFileno());
                    _subscribeFlags &= ~SUBSCRIBE_FLAG_INPUT;
                    _process.closeInput();

                    _client->_application._dispatcher.subscribe(_process.getOutputFileno(), EPOLLIN | EPOLLHUP, this);
                    _subscribeFlags |= SUBSCRIBE_FLAG_OUTPUT;
                }
#ifdef __42_LIKES_WASTING_CPU_CYCLES__
                return;
#endif // __42_LIKES_WASTING_CPU_CYCLES__
            }
            if (eventMask & EPOLLIN)
            {
                char buffer[8192];

                // Read up to 8KiB from the process' standard output pipe
                ssize_t result = read(_process.getOutputFileno(), buffer, sizeof(buffer));
                if (result < 0)
                {
                    if (SignalManager::shouldQuit())
                        return;
                    throw std::runtime_error("Unable to read from CGI process");
                }
                if (result == 0)
                {
                    // This can never happen, see the following example:
                    // - Child process exits with status 0
                    //   The pipes are closed as a side-effect
                    // - Due to the pipes closing, an EPOLLIN or EPOLLOUT event is generated
                    // - The event handler (this function) calls `_process.getStatus()`
                    //   `Process` will call `waitpid()` which leads to `PROCESS_EXIT_SUCCESS`
                    //   and thus, never to `PROCESS_RUNNING` again
                    // - The zero-length read event is never processed and the process is marked
                    //   for destruction, leading to its destructor being called after the event
                    //   buffer is fully processed by `Dispatcher`
                    throw std::runtime_error("Unexpected end of stream");
                }
                size_t length = static_cast<size_t>(result);

                // Push the data into the response buffer
                size_t oldLength = _buffer.size();
                if (SIZE_MAX - oldLength < length)
                    throw std::runtime_error("Response body too large");
                size_t newLength = oldLength + length;
                if (newLength > (2ull * 1024ull * 1024ull * 1024ull))
                    throw std::runtime_error("Response body too large");
                _buffer.resize(oldLength + length);
                std::memcpy(&_buffer[oldLength], buffer, length);
#ifdef __42_LIKES_WASTING_CPU_CYCLES__
                return;
#endif // __42_LIKES_WASTING_CPU_CYCLES__
            }
            break;
        case PROCESS_EXIT_SUCCESS:
            _state = CGI_PROCESS_SUCCESS;
            _client->handleCgiState();
            break;
        case PROCESS_EXIT_FAILURE:
            _state = CGI_PROCESS_FAILURE;
            _client->handleCgiState();
            break;
    }
}

/* Handles an exception that occurred in `handleEvent()` */
void CgiProcess::handleException(const char *message)
{
    (void)message;
    //std::cout << "Exception while handling CGI process event: " << message << std::endl;

    // Only report failure once
    if (_state != CGI_PROCESS_RUNNING)
    {
        _state = CGI_PROCESS_FAILURE;
        try
        {
            _client->handleCgiState();
        }
        // In case of catastrophic failure, drop the client
        catch (const std::exception &exception)
        {
            _client->handleException(exception.what());
        }
        catch (...)
        {
            _client->handleException("Thrown type is not derived from std::exception");
        }
    }
}

/* Transitions the process into timeout state */
void CgiProcess::handleTimeout()
{
    if (_state != CGI_PROCESS_RUNNING)
        return;
    _state = CGI_PROCESS_TIMEOUT;
    _client->handleCgiState();
}

/* Creates a vector of strings for the process arguments */
std::vector<std::string> CgiProcess::setupArguments(const HttpRequest &request, const RoutingInfo &routingInfo, const std::string &fileName)
{
    (void)request;
    std::vector<std::string> result;
    result.push_back(routingInfo.cgiInterpreter);
    result.push_back(fileName);
    if (request.method == HTTP_METHOD_GET && !request.queryParameters.isEmpty())
        result.push_back('?' + request.queryParameters.toString());
    return result;
}

/* Creates a vector of strings for the process environment */
std::vector<std::string> CgiProcess::setupEnvironment(const HttpRequest &request, const RoutingInfo &routingInfo)
{
    std::vector<std::string> result;

    const HttpRequest::Header *contentType = request.findHeader(C_SLICE("Content-Type"));
    const HttpRequest::Header *host = request.findHeader(C_SLICE("Host"));

    Slice nodePath(routingInfo.nodePath);
    Slice filename;
    if (!nodePath.splitEnd('/', filename))
        filename = nodePath;

    // Add the standard CGI environment variables
    result.push_back("AUTH_TYPE=");
    result.push_back("CONTENT_LENGTH=" + Utility::numberToString(request.body.size()));
    if (contentType != NULL)
        result.push_back("CONTENT_TYPE=" + contentType->getValue());
    result.push_back("GATEWAY_INTERFACE=CGI/1.1");
    result.push_back("PATH_INFO=");
    result.push_back("PATH_TRANSLATED=");
    result.push_back("QUERY_STRING=" + request.queryParameters.toString());
    result.push_back("REMOTE_ADDR=" + Utility::ipv4ToString(request.clientHost));
    result.push_back("REMOTE_HOST=" + Utility::ipv4ToString(request.clientHost));
    result.push_back("REQUEST_METHOD=" + std::string(httpMethodToString(request.method)));
    result.push_back("SCRIPT_NAME=" + request.queryPath);
    result.push_back("SCRIPT_FILENAME=" + filename.toString());
    if (host != NULL)
        result.push_back("HTTP_HOST=" + host->getValue());
    else
        result.push_back("HTTP_HOST=NULL");
    if (host != NULL)
        result.push_back("SERVER_NAME=" + host->getValue());
    result.push_back("SERVER_PORT=" + Utility::numberToString(routingInfo.serverConfig->port));
    result.push_back("SERVER_PROTOCOL=HTTP/1.1");
    result.push_back("SERVER_SOFTWARE=webs3rv/1.0");
    result.push_back("REDIRECT_STATUS=200");

    // Add the HTTP request headers to the environment
    std::vector<HttpRequest::Header>::const_iterator header = request.headers.begin();
    for (; header != request.headers.end(); header++)
    {
        std::string key = "HTTP_" + header->getKey();
        std::string value = header->getValue();
        for (size_t i = 0; i < key.size(); i++)
        {
            if (key[i] == '-')
                key[i] = '_';
            else
                key[i] = std::toupper(key[i]);
        }
        result.push_back(key + "=" + value);
    }

    return result;
}
