#include "mcp/McpProtocol.h"

#include <algorithm>
#include <cctype>

namespace Monolith
{
  namespace Mcp
  {

    using json = nlohmann::json;

    namespace
    {

      McpHttpResponse MakeJsonResponse(int statusCode,
                                       const std::string &statusText,
                                       const json &body)
      {
        McpHttpResponse response;
        response.statusCode = statusCode;
        response.statusText = statusText;
        response.contentType = "application/json";
        response.body = body.dump();
        return response;
      }

      json MakeSuccess(const json &id, const json &result)
      {
        return json{{"jsonrpc", "2.0"}, {"id", id}, {"result", result}};
      }

      json MakeError(const json &id, int code, const std::string &message)
      {
        return json{
            {"jsonrpc", "2.0"},
            {"id", id},
            {"error", {{"code", code}, {"message", message}}},
        };
      }

      json BuildToolList()
      {
        return json{
            {"tools", json::array({
                          {
                              {"name", "editor.search"},
                              {"description", "Search compact scene objects and assets."},
                              {"inputSchema", {
                                                  {"type", "object"},
                                                  {"properties", {
                                                                     {"query", {{"type", "string"}}},
                                                                     {"limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 25}}},
                                                                     {"scope", {{"type", "string"}, {"enum", json::array({"all", "objects", "assets"})}}},
                                                                 }},
                                              }},
                          },
                          {
                              {"name", "editor.get_object"},
                              {"description", "Fetch one object by id."},
                              {"inputSchema", {
                                                  {"type", "object"},
                                                  {"required", json::array({"id"})},
                                                  {"properties", {{"id", {{"type", "string"}}}}},
                                              }},
                          },
                          {
                              {"name", "editor.select"},
                              {"description", "Update editor selection."},
                              {"inputSchema", {
                                                  {"type", "object"},
                                                  {"properties", {
                                                                     {"id", {{"type", "string"}}},
                                                                     {"ids", {{"type", "array"}, {"items", {{"type", "string"}}}}},
                                                                 }},
                                              }},
                          },
                          {
                              {"name", "editor.create_object"},
                              {"description", "Create one scene object."},
                              {"inputSchema", {
                                                  {"type", "object"},
                                                  {"required", json::array({"type"})},
                                                  {"properties", {
                                                                     {"type", {{"type", "string"}}},
                                                                     {"id", {{"type", "string"}}},
                                                                     {"assetId", {{"type", "string"}}},
                                                                     {"parentId", {{"type", "string"}}},
                                                                     {"position", {{"type", "array"}}},
                                                                     {"scale", {{"type", "array"}}},
                                                                     {"yaw", {{"type", "number"}}},
                                                                     {"pitch", {{"type", "number"}}},
                                                                     {"roll", {{"type", "number"}}},
                                                                     {"props", {{"type", "object"}}},
                                                                     {"components", {{"type", "array"}}},
                                                                 }},
                                              }},
                          },
                          {
                              {"name", "editor.update_object"},
                              {"description", "Patch a scene object by id."},
                              {"inputSchema", {
                                                  {"type", "object"},
                                                  {"required", json::array({"id"})},
                                                  {"properties", {
                                                                     {"id", {{"type", "string"}}},
                                                                     {"assetId", {{"type", "string"}}},
                                                                     {"position", {{"type", "array"}}},
                                                                     {"scale", {{"type", "array"}}},
                                                                     {"yaw", {{"type", "number"}}},
                                                                     {"pitch", {{"type", "number"}}},
                                                                     {"roll", {{"type", "number"}}},
                                                                     {"props", {{"type", "object"}}},
                                                                     {"components", {{"type", "array"}}},
                                                                 }},
                                              }},
                          },
                          {
                              {"name", "editor.transform"},
                              {"description", "Apply transform changes to one object."},
                              {"inputSchema", {
                                                  {"type", "object"},
                                                  {"required", json::array({"id"})},
                                                  {"properties", {
                                                                     {"id", {{"type", "string"}}},
                                                                     {"position", {{"type", "array"}}},
                                                                     {"scale", {{"type", "array"}}},
                                                                     {"yaw", {{"type", "number"}}},
                                                                     {"pitch", {{"type", "number"}}},
                                                                     {"roll", {{"type", "number"}}},
                                                                 }},
                                              }},
                          },
                          {
                              {"name", "editor.duplicate"},
                              {"description", "Duplicate one object."},
                              {"inputSchema", {
                                                  {"type", "object"},
                                                  {"required", json::array({"id"})},
                                                  {"properties", {
                                                                     {"id", {{"type", "string"}}},
                                                                     {"count", {{"type", "integer"}, {"minimum", 1}, {"maximum", 8}}},
                                                                 }},
                                              }},
                          },
                          {
                              {"name", "editor.delete"},
                              {"description", "Delete scene objects by id."},
                              {"inputSchema", {
                                                  {"type", "object"},
                                                  {"properties", {
                                                                     {"id", {{"type", "string"}}},
                                                                     {"ids", {{"type", "array"}, {"items", {{"type", "string"}}}}},
                                                                 }},
                                              }},
                          },
                          {
                              {"name", "editor.save_scene"},
                              {"description", "Save the active scene document."},
                              {"inputSchema", {{"type", "object"}}},
                          },
                          {
                              {"name", "editor.reload_scene"},
                              {"description", "Queue an in-editor scene reload."},
                              {"inputSchema", {{"type", "object"}}},
                          },
                      })},
        };
      }

      json BuildResourceList()
      {
        return json{
            {"resources", json::array({
                              {{"uri", "scene://summary"}, {"name", "scene.summary"}, {"mimeType", "application/json"}},
                              {{"uri", "scene://selection"}, {"name", "scene.selection"}, {"mimeType", "application/json"}},
                              {{"uri", "scene://assets"}, {"name", "scene.assets"}, {"mimeType", "application/json"}},
                              {{"uri", "console://recent"}, {"name", "console.recent"}, {"mimeType", "application/json"}},
                          })},
        };
      }

      json BuildInitializeResult()
      {
        return json{
            {"protocolVersion", "2024-11-05"},
            {"serverInfo", {{"name", "horo-engine"}, {"version", "0.1.0"}}},
            {"capabilities", {
                                 {"resources", {{"listChanged", false}}},
                                 {"tools", {{"listChanged", false}}},
                             }},
        };
      }

      json BuildResourceReadResult(const McpEditorSnapshot &snapshot, const std::string &uri)
      {
        json payload = json::object();
        if (uri == "scene://summary")
          payload = BuildSceneSummaryJson(snapshot);
        else if (uri == "scene://selection")
          payload = BuildSelectionJson(snapshot);
        else if (uri == "scene://assets")
          payload = BuildAssetsJson(snapshot);
        else if (uri == "console://recent")
          payload = BuildConsoleJson(snapshot);

        return json{
            {"contents", json::array({
                             {{"uri", uri}, {"mimeType", "application/json"}, {"text", payload.dump()}},
                         })},
        };
      }

      json BuildTextToolResult(const json &payload)
      {
        return json{
            {"content", json::array({
                            {{"type", "text"}, {"text", payload.dump()}},
                        })},
            {"structuredContent", payload},
        };
      }

    } // namespace

    McpProtocol::McpProtocol(McpProtocolContext context) : m_context(std::move(context)) {}

    McpHttpResponse McpProtocol::HandleHttp(const McpHttpRequest &request) const
    {
      if (request.path != "/mcp")
        return MakeJsonResponse(404, "Not Found", json{{"error", "Unknown MCP endpoint."}});

      if (request.method == "GET")
        return MakeJsonResponse(200, "OK", json{{"name", "horo-engine"}, {"transport", "http"}});
      if (request.method != "POST")
        return MakeJsonResponse(405, "Method Not Allowed", json{{"error", "Use POST /mcp."}});

      json payload = json::object();
      try
      {
        payload = json::parse(request.body.empty() ? "{}" : request.body);
      }
      catch (const json::exception &e)
      {
        if (m_context.activitySink)
          m_context.activitySink("parse", false, e.what());
        return MakeJsonResponse(400, "Bad Request", json{{"error", e.what()}});
      }

      const json id = payload.contains("id") ? payload["id"] : nullptr;
      const std::string method = payload.value("method", std::string());
      const json params = payload.value("params", json::object());

      if (method.rfind("notifications/", 0) == 0)
      {
        if (m_context.activitySink)
          m_context.activitySink(method, true, "notification");
        McpHttpResponse response;
        response.statusCode = 202;
        response.statusText = "Accepted";
        return response;
      }

      if (method == "initialize")
      {
        if (m_context.activitySink)
          m_context.activitySink(method, true, "ok");
        return MakeJsonResponse(200, "OK", MakeSuccess(id, BuildInitializeResult()));
      }
      if (method == "ping")
      {
        if (m_context.activitySink)
          m_context.activitySink(method, true, "ok");
        return MakeJsonResponse(200, "OK", MakeSuccess(id, json::object()));
      }
      if (method == "resources/list")
      {
        if (m_context.activitySink)
          m_context.activitySink(method, true, "ok");
        return MakeJsonResponse(200, "OK", MakeSuccess(id, BuildResourceList()));
      }
      if (method == "tools/list")
      {
        if (m_context.activitySink)
          m_context.activitySink(method, true, "ok");
        return MakeJsonResponse(200, "OK", MakeSuccess(id, BuildToolList()));
      }

      const std::shared_ptr<const McpEditorSnapshot> snapshot =
          m_context.snapshotProvider ? m_context.snapshotProvider() : nullptr;
      if (!snapshot)
      {
        if (m_context.activitySink)
          m_context.activitySink(method, false, "snapshot unavailable");
        return MakeJsonResponse(503, "Service Unavailable",
                                MakeError(id, -32001, "Editor snapshot unavailable."));
      }

      if (method == "resources/read")
      {
        const std::string uri = params.value("uri", std::string());
        if (uri != "scene://summary" && uri != "scene://selection" && uri != "scene://assets" &&
            uri != "console://recent")
        {
          if (m_context.activitySink)
            m_context.activitySink(method, false, uri);
          return MakeJsonResponse(200, "OK", MakeError(id, -32602, "Unknown resource URI."));
        }
        if (m_context.activitySink)
          m_context.activitySink(method, true, uri);
        return MakeJsonResponse(200, "OK", MakeSuccess(id, BuildResourceReadResult(*snapshot, uri)));
      }

      if (method == "tools/call")
      {
        const std::string name = params.value("name", std::string());
        const json arguments = params.value("arguments", json::object());
        if (name == "editor.search")
        {
          const std::string query = arguments.value("query", std::string());
          const size_t limit = static_cast<size_t>(arguments.value("limit", 8));
          const std::string scope = arguments.value("scope", std::string("all"));
          const json result = SearchSnapshot(*snapshot, query, limit, scope);
          if (m_context.activitySink)
            m_context.activitySink(name, true, query);
          return MakeJsonResponse(200, "OK", MakeSuccess(id, BuildTextToolResult(result)));
        }
        if (name == "editor.get_object")
        {
          const std::string objectId = arguments.value("id", std::string());
          const McpObjectSnapshot *object = FindObjectById(*snapshot, objectId);
          if (!object)
          {
            if (m_context.activitySink)
              m_context.activitySink(name, false, objectId);
            return MakeJsonResponse(200, "OK", MakeError(id, -32602, "Object not found."));
          }
          if (m_context.activitySink)
            m_context.activitySink(name, true, objectId);
          return MakeJsonResponse(200, "OK", MakeSuccess(id, BuildTextToolResult(BuildObjectJson(*object))));
        }

        const bool isWriteTool = name == "editor.select" || name == "editor.create_object" ||
                                 name == "editor.update_object" || name == "editor.transform" ||
                                 name == "editor.duplicate" || name == "editor.delete" ||
                                 name == "editor.save_scene" || name == "editor.reload_scene";
        if (!isWriteTool)
        {
          if (m_context.activitySink)
            m_context.activitySink(name, false, "unsupported");
          return MakeJsonResponse(200, "OK", MakeError(id, -32601, "Unknown tool."));
        }

        if (!m_context.commandInvoker)
          return MakeJsonResponse(503, "Service Unavailable",
                                  MakeError(id, -32002, "Command queue unavailable."));

        const McpCommandResult result = m_context.commandInvoker(name, arguments);
        if (m_context.activitySink)
          m_context.activitySink(name, result.ok, result.ok ? "ok" : result.error);
        if (!result.ok)
          return MakeJsonResponse(200, "OK", MakeError(id, -32010, result.error));
        return MakeJsonResponse(200, "OK", MakeSuccess(id, BuildTextToolResult(result.data)));
      }

      if (m_context.activitySink)
        m_context.activitySink(method, false, "unknown method");
      return MakeJsonResponse(200, "OK", MakeError(id, -32601, "Method not found."));
    }

  } // namespace Mcp
} // namespace Monolith
