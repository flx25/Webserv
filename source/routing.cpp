#include "routing.hpp"
#include "utility.hpp"

/* Sets the route pointer to the given local route and its node type; also sets the status */
void RoutingInfo::setLocalRoute(const LocalRouteConfig *localRouteConfig, NodeType nodeType)
{
    status = ROUTING_STATUS_FOUND_LOCAL;
    _opaqueRoute = reinterpret_cast<const void *>(localRouteConfig);
    _nodeType = nodeType;
}

/* Sets the route pointer to the given redirect route; also sets the status */
void RoutingInfo::setRedirectRoute(const RedirectRouteConfig *redirectRouteConfig)
{
    status = ROUTING_STATUS_FOUND_REDIRECT;
    _opaqueRoute = reinterpret_cast<const void *>(redirectRouteConfig);
}

/* Finds a route on a server configuration using the given query path */
RoutingInfo RoutingInfo::findRoute(const ServerConfig &serverConfig, Slice queryPath)
{
    RoutingInfo info;
    NodeType    nodeType;
    size_t      bestLength = 0;

    // Set initial info
    info.status = ROUTING_STATUS_NOT_FOUND;
    info.serverConfig = &serverConfig;

    // Search for a local route
    for (size_t index = 0; index < serverConfig.localRoutes.size(); index++)
    {
        const LocalRouteConfig &config = serverConfig.localRoutes[index];

        // Ignore the route if there is a better alternative or if the initial path doesn't match
        if (bestLength > config.path.size())
            continue;
        if (!queryPath.startsWith(config.path))
            continue;

        // Build the full node path
        std::string path = config.rootDirectory + "/" + queryPath.cut(config.path.size()).stripStart('/').stripEnd('/').toString();
        nodeType = Utility::queryNodeType(path);

        // Return early when a node exists but is not accessible
        if (nodeType == NODE_TYPE_NO_ACCESS || nodeType == NODE_TYPE_UNSUPPORTED)
        {
            info.status = ROUTING_STATUS_NO_ACCESS;
            return info;
        }

        // Ignore routes where 
        if (nodeType != NODE_TYPE_REGULAR && nodeType != NODE_TYPE_DIRECTORY)
            continue;

        // Populate with the current route
        info.hasCgiInterpreter = false;
        bestLength = config.path.size();
        info.nodePath = path;
        info.setLocalRoute(&config, nodeType);

        // Search for the CGI interpreter
        std::map<std::string, std::string>::const_iterator iterator = config.cgiTypes.begin();
        for (; iterator != config.cgiTypes.end(); iterator++)
        {
            if (queryPath.endsWith(iterator->first))
            {
                info.hasCgiInterpreter = true;
                info.cgiInterpreter = iterator->second;
                break;
            }
        }
    }

    // Search for a redirect route
    for (size_t index = 0; index < serverConfig.redirectRoutes.size(); index++)
    {
        const RedirectRouteConfig &config = serverConfig.redirectRoutes[index];

        // Ignore the route if there is a better alternative or if the initial path doesn't match
        if (bestLength > config.path.size())
            continue;
        if (!queryPath.startsWith(config.path))
            continue;

        // Populate with the current route
        info.hasCgiInterpreter = false;
        bestLength = config.path.size();
        info.setRedirectRoute(&config);
    }

    return info;
}
