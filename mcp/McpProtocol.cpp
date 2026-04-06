#include "mcp/McpProtocol.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <unordered_set>

namespace Monolith {
namespace Mcp {

using json = nlohmann::json;

namespace {

std::string ToLowerAscii(std::string value) {
  for (char& c : value)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return value;
}

std::string JsonToCompactString(const json& value) {
  if (value.is_null())
    return "null";
  if (value.is_string())
    return value.get<std::string>();
  return value.dump();
}

std::string TruncatePreview(const std::string& text, size_t maxChars = 240) {
  if (text.size() <= maxChars)
    return text;
  if (maxChars < 4)
    return text.substr(0, maxChars);
  return text.substr(0, maxChars - 3) + "...";
}

McpHttpResponse MakeJsonResponse(int statusCode, const std::string& statusText, const json& body) {
  McpHttpResponse response;
  response.statusCode = statusCode;
  response.statusText = statusText;
  response.contentType = "application/json";
  response.body = body.dump();
  return response;
}

json MakeSuccess(const json& id, const json& result) {
  return json{{"jsonrpc", "2.0"}, {"id", id}, {"result", result}};
}

json MakeError(const json& id, int code, const std::string& message) {
  return json{{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", code}, {"message", message}}}};
}

const std::vector<McpCatalogEntry>& GetToolCatalog() {
  static const std::vector<McpCatalogEntry> catalog = {
      {"editor.search", "tool", "Search compact scene objects and assets."},
      {"editor.get_object", "tool", "Fetch one object by id with full MCP detail."},
      {"editor.get_object_edges", "tool",
       "Return one object's world-space corners and edge segments."},
      {"editor.list_objects", "tool", "List object summaries with optional filters and limits."},
      {"editor.get_objects", "tool", "Fetch multiple objects by id."},
      {"editor.get_object_children", "tool", "List direct children of one object."},
      {"editor.get_object_parent", "tool", "Fetch the parent summary of one object."},
      {"editor.count_objects", "tool", "Count objects with optional type filtering."},
      {"editor.select", "tool", "Update editor object selection."},
      {"editor.clear_selection", "tool", "Clear object and asset selection."},
      {"editor.create_object", "tool", "Create one scene object."},
      {"editor.create_object_from_asset", "tool", "Create a prop from an existing asset id."},
      {"editor.update_object", "tool", "Patch a scene object by id."},
      {"editor.transform", "tool", "Apply transform changes to one object."},
      {"editor.rename_object", "tool", "Rename a scene object id."},
      {"editor.reparent_object", "tool", "Set or clear an object's parent."},
      {"editor.duplicate", "tool", "Duplicate one object."},
      {"editor.delete", "tool", "Delete scene objects by id."},
      {"editor.list_assets", "tool", "List asset summaries with optional filters."},
      {"editor.get_asset", "tool", "Fetch one asset by id."},
      {"editor.search_assets", "tool", "Search assets by id, mesh or texture."},
      {"editor.count_assets", "tool", "Count assets with optional query filtering."},
      {"editor.select_asset", "tool", "Select an asset in the editor."},
      {"editor.update_asset", "tool", "Patch one asset definition."},
      {"editor.delete_asset", "tool", "Delete one asset definition."},
      {"editor.scene_status", "tool", "Return compact scene status."},
      {"editor.get_scene_file", "tool", "Return the active scene file path and save state."},
      {"editor.new_scene", "tool", "Create a new empty scene document."},
      {"editor.save_scene", "tool", "Save the active scene document."},
      {"editor.reload_scene", "tool", "Queue an in-editor scene reload."},
      {"editor.search_console", "tool", "Search recent console output."},
  };
  return catalog;
}

const std::vector<McpCatalogEntry>& GetResourceCatalog() {
  static const std::vector<McpCatalogEntry> catalog = {
      {"scene.summary", "scene://summary", "Compact scene overview with object sample."},
      {"scene.selection", "scene://selection", "Current selected objects and asset."},
      {"scene.assets", "scene://assets", "Compact asset summary sample."},
      {"scene.hierarchy", "scene://hierarchy", "Flat hierarchy preview with depth and child counts."},
      {"scene.objects", "scene://objects", "Filtered object list summary."},
      {"scene.scene_status", "scene://scene_status", "Scene status flags and counts."},
      {"assets.selection", "assets://selection", "Current selected asset summary."},
      {"assets.catalog", "assets://catalog", "Filtered asset catalog summary."},
      {"console.recent", "console://recent", "Recent console lines."},
      {"console.summary", "console://summary", "Console severity counts and latest lines."},
  };
  return catalog;
}

json BuildInitializeResult() {
  return json{{"protocolVersion", "2024-11-05"},
              {"serverInfo", {{"name", "horo-engine"}, {"version", "0.2.0"}}},
              {"capabilities", {{"resources", {{"listChanged", false}}}, {"tools", {{"listChanged", false}}}}}};
}

json BuildTextToolResult(const json& payload) {
  return json{{"content", json::array({{{"type", "text"}, {"text", payload.dump()}}})},
              {"structuredContent", payload}};
}

std::string GetParentId(const McpObjectSnapshot& object) {
  const auto it = object.props.find("parentId");
  if (it == object.props.end())
    return {};
  return it->second;
}

bool MatchesObjectQuery(const McpObjectSnapshot& object, const std::string& query) {
  if (query.empty())
    return true;
  const std::string lowered = ToLowerAscii(query);
  return ToLowerAscii(object.id).find(lowered) != std::string::npos ||
         ToLowerAscii(object.type).find(lowered) != std::string::npos ||
         ToLowerAscii(object.assetId).find(lowered) != std::string::npos ||
         ToLowerAscii(GetParentId(object)).find(lowered) != std::string::npos;
}

bool IsKnownResourceUri(const std::string& uri) {
  static const std::unordered_set<std::string> uris = {
      "scene://summary",  "scene://selection",   "scene://assets",      "scene://hierarchy",
      "scene://objects",  "scene://scene_status","assets://selection",  "assets://catalog",
      "console://recent", "console://summary",
  };
  return uris.find(uri) != uris.end();
}

json BuildToolList() {
  json tools = json::array();
  for (const McpCatalogEntry& entry : GetToolCatalog()) {
    json tool = {{"name", entry.name}, {"description", entry.description}, {"inputSchema", {{"type", "object"}}}};
    if (entry.name == "editor.search") {
      tool["inputSchema"]["properties"] = {{"query", {{"type", "string"}}},
                                           {"limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 25}}},
                                           {"scope", {{"type", "string"}, {"enum", json::array({"all", "objects", "assets"})}}}};
    } else if (entry.name == "editor.get_object" || entry.name == "editor.get_object_edges" ||
               entry.name == "editor.get_asset" ||
               entry.name == "editor.get_object_children" || entry.name == "editor.get_object_parent" ||
               entry.name == "editor.rename_object" || entry.name == "editor.reparent_object" ||
               entry.name == "editor.select_asset" || entry.name == "editor.update_asset" ||
               entry.name == "editor.delete_asset") {
      tool["inputSchema"]["required"] = json::array({"id"});
      tool["inputSchema"]["properties"] = {{"id", {{"type", "string"}}}};
      if (entry.name == "editor.rename_object")
        tool["inputSchema"]["properties"]["newId"] = {{"type", "string"}};
      if (entry.name == "editor.reparent_object")
        tool["inputSchema"]["properties"]["parentId"] = {{"type", "string"}};
      if (entry.name == "editor.get_object_children")
        tool["inputSchema"]["properties"]["limit"] = {{"type", "integer"}, {"minimum", 1}, {"maximum", 64}};
      if (entry.name == "editor.update_asset") {
        tool["inputSchema"]["properties"]["mesh"] = {{"type", "string"}};
        tool["inputSchema"]["properties"]["renderScale"] = {{"type", "string"}};
        tool["inputSchema"]["properties"]["albedoMap"] = {{"type", "string"}};
      }
      if (entry.name == "editor.rename_object")
        tool["inputSchema"]["required"] = json::array({"id", "newId"});
    } else if (entry.name == "editor.get_objects") {
      tool["inputSchema"]["required"] = json::array({"ids"});
      tool["inputSchema"]["properties"] = {{"ids", {{"type", "array"}, {"items", {{"type", "string"}}}}}};
    } else if (entry.name == "editor.list_objects") {
      tool["inputSchema"]["properties"] = {{"limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 64}}},
                                           {"type", {{"type", "string"}}},
                                           {"query", {{"type", "string"}}},
                                           {"selectedOnly", {{"type", "boolean"}}}};
    } else if (entry.name == "editor.count_objects") {
      tool["inputSchema"]["properties"] = {{"type", {{"type", "string"}}}, {"query", {{"type", "string"}}}};
    } else if (entry.name == "editor.select" || entry.name == "editor.delete") {
      tool["inputSchema"]["properties"] = {{"id", {{"type", "string"}}},
                                           {"ids", {{"type", "array"}, {"items", {{"type", "string"}}}}}};
    } else if (entry.name == "editor.create_object") {
      tool["inputSchema"]["required"] = json::array({"type"});
      tool["inputSchema"]["properties"] = {{"type", {{"type", "string"}}},
                                           {"id", {{"type", "string"}}},
                                           {"assetId", {{"type", "string"}}},
                                           {"parentId", {{"type", "string"}}},
                                           {"position", {{"type", "array"}}},
                                           {"scale", {{"type", "array"}}},
                                           {"yaw", {{"type", "number"}}},
                                           {"pitch", {{"type", "number"}}},
                                           {"roll", {{"type", "number"}}},
                                           {"props", {{"type", "object"}}},
                                           {"components", {{"type", "array"}}}};
    } else if (entry.name == "editor.create_object_from_asset") {
      tool["inputSchema"]["required"] = json::array({"assetId"});
      tool["inputSchema"]["properties"] = {{"assetId", {{"type", "string"}}},
                                           {"parentId", {{"type", "string"}}},
                                           {"id", {{"type", "string"}}},
                                           {"position", {{"type", "array"}}},
                                           {"yaw", {{"type", "number"}}},
                                           {"pitch", {{"type", "number"}}},
                                           {"roll", {{"type", "number"}}}};
    } else if (entry.name == "editor.update_object" || entry.name == "editor.transform") {
      tool["inputSchema"]["required"] = json::array({"id"});
      tool["inputSchema"]["properties"] = {{"id", {{"type", "string"}}},
                                           {"assetId", {{"type", "string"}}},
                                           {"position", {{"type", "array"}}},
                                           {"scale", {{"type", "array"}}},
                                           {"yaw", {{"type", "number"}}},
                                           {"pitch", {{"type", "number"}}},
                                           {"roll", {{"type", "number"}}},
                                           {"props", {{"type", "object"}}},
                                           {"components", {{"type", "array"}}}};
    } else if (entry.name == "editor.duplicate") {
      tool["inputSchema"]["required"] = json::array({"id"});
      tool["inputSchema"]["properties"] = {{"id", {{"type", "string"}}},
                                           {"count", {{"type", "integer"}, {"minimum", 1}, {"maximum", 8}}}};
    } else if (entry.name == "editor.list_assets" || entry.name == "editor.search_assets" ||
               entry.name == "editor.search_console") {
      tool["inputSchema"]["properties"] = {{"query", {{"type", "string"}}},
                                           {"limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 64}}}};
    } else if (entry.name == "editor.count_assets") {
      tool["inputSchema"]["properties"] = {{"query", {{"type", "string"}}}};
    } else if (entry.name == "editor.new_scene") {
      tool["inputSchema"]["properties"] = {{"sceneName", {{"type", "string"}}},
                                           {"sceneId", {{"type", "string"}}}};
    }
    tools.push_back(std::move(tool));
  }
  return json{{"tools", std::move(tools)}};
}

json BuildResourceList() {
  json resources = json::array();
  for (const McpCatalogEntry& entry : GetResourceCatalog())
    resources.push_back({{"uri", entry.target}, {"name", entry.name}, {"mimeType", "application/json"}});
  return json{{"resources", std::move(resources)}};
}

json BuildResourcePayload(const McpEditorSnapshot& snapshot, const std::string& uri, const json& params) {
  if (uri == "scene://summary")
    return BuildSceneSummaryJson(snapshot, params.value("limit", 12));
  if (uri == "scene://selection")
    return BuildSelectionJson(snapshot);
  if (uri == "scene://assets")
    return BuildAssetsJson(snapshot, params.value("limit", 12));
  if (uri == "scene://hierarchy")
    return BuildHierarchyJson(snapshot, params.value("limit", 32));
  if (uri == "scene://objects")
    return BuildObjectListJson(snapshot, params.value("limit", 12), params.value("type", std::string()),
                               params.value("query", std::string()), params.value("selectedOnly", false));
  if (uri == "scene://scene_status")
    return BuildSceneStatusJson(snapshot);
  if (uri == "assets://selection")
    return BuildAssetsSelectionJson(snapshot);
  if (uri == "assets://catalog")
    return BuildAssetsCatalogJson(snapshot, params.value("limit", 12), params.value("query", std::string()));
  if (uri == "console://recent")
    return BuildConsoleJson(snapshot, params.value("limit", 20));
  if (uri == "console://summary")
    return BuildConsoleSummaryJson(snapshot, params.value("limit", 5));
  return json::object();
}

json BuildResourceReadResult(const McpEditorSnapshot& snapshot, const std::string& uri, const json& params) {
  const json payload = BuildResourcePayload(snapshot, uri, params);
  return json{{"contents", json::array({{{"uri", uri}, {"mimeType", "application/json"}, {"text", payload.dump()}}})}};
}

}  // namespace

McpProtocol::McpProtocol(McpProtocolContext context) : m_context(std::move(context)) {}

size_t McpProtocol::ToolCount() const {
  return GetToolCatalog().size();
}

size_t McpProtocol::ResourceCount() const {
  return GetResourceCatalog().size();
}

const std::vector<McpCatalogEntry>& McpProtocol::ToolCatalog() const {
  return GetToolCatalog();
}

const std::vector<McpCatalogEntry>& McpProtocol::ResourceCatalog() const {
  return GetResourceCatalog();
}

McpHttpResponse McpProtocol::HandleHttp(const McpHttpRequest& request) const {
  const auto startedAt = std::chrono::steady_clock::now();
  McpActivityRecord activity;
  activity.transportMethod = request.method;
  activity.requestPreview = TruncatePreview(request.body.empty() ? "{}" : request.body);

  auto finish = [&](const std::string& operation,
                    const std::string& target,
                    const std::string& mcpMethod,
                    const json& id,
                    bool ok,
                    int httpStatus,
                    const std::string& error,
                    const json* responsePayload,
                    const std::string& responseText) {
    activity.operation = operation;
    activity.target = target;
    activity.mcpMethod = mcpMethod;
    activity.requestId = id.is_null() ? std::string() : TruncatePreview(JsonToCompactString(id), 64);
    activity.ok = ok;
    activity.httpStatus = httpStatus;
    activity.error = error;
    activity.durationMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - startedAt).count();
    activity.responsePreview =
        responsePayload ? TruncatePreview(responsePayload->dump()) : TruncatePreview(responseText);
    if (m_context.activitySink)
      m_context.activitySink(activity);
  };

  if (request.path != "/mcp") {
    const json body = json{{"error", "Unknown MCP endpoint."}};
    finish("http", request.path, std::string(), nullptr, false, 404, "Unknown MCP endpoint.", &body, {});
    return MakeJsonResponse(404, "Not Found", body);
  }
  if (request.method == "GET") {
    const json body = json{{"name", "horo-engine"}, {"transport", "http"}};
    finish("http", "transport.info", std::string(), nullptr, true, 200, std::string(), &body, {});
    return MakeJsonResponse(200, "OK", body);
  }
  if (request.method != "POST") {
    const json body = json{{"error", "Use POST /mcp."}};
    finish("http", request.method, std::string(), nullptr, false, 405, "Use POST /mcp.", &body, {});
    return MakeJsonResponse(405, "Method Not Allowed", body);
  }

  json payload = json::object();
  try {
    payload = json::parse(request.body.empty() ? "{}" : request.body);
  } catch (const json::exception& e) {
    const json body = json{{"error", e.what()}};
    finish("parse", "parse", std::string(), nullptr, false, 400, e.what(), &body, {});
    return MakeJsonResponse(400, "Bad Request", body);
  }

  const json id = payload.contains("id") ? payload["id"] : nullptr;
  const std::string method = payload.value("method", std::string());
  const json params = payload.value("params", json::object());
  activity.requestId = id.is_null() ? std::string() : TruncatePreview(JsonToCompactString(id), 64);
  activity.mcpMethod = method;
  activity.requestPreview = TruncatePreview(params.dump());

  if (method.rfind("notifications/", 0) == 0) {
    finish("notification", method, method, id, true, 202, std::string(), nullptr, "accepted");
    McpHttpResponse response;
    response.statusCode = 202;
    response.statusText = "Accepted";
    return response;
  }
  if (method == "initialize") {
    const json result = MakeSuccess(id, BuildInitializeResult());
    finish("initialize", "initialize", method, id, true, 200, std::string(), &result, {});
    return MakeJsonResponse(200, "OK", result);
  }
  if (method == "ping") {
    const json result = MakeSuccess(id, json::object());
    finish("ping", "ping", method, id, true, 200, std::string(), &result, {});
    return MakeJsonResponse(200, "OK", result);
  }
  if (method == "resources/list") {
    const json result = MakeSuccess(id, BuildResourceList());
    finish("resource.list", "resources/list", method, id, true, 200, std::string(), &result, {});
    return MakeJsonResponse(200, "OK", result);
  }
  if (method == "tools/list") {
    const json result = MakeSuccess(id, BuildToolList());
    finish("tool.list", "tools/list", method, id, true, 200, std::string(), &result, {});
    return MakeJsonResponse(200, "OK", result);
  }

  const std::shared_ptr<const McpEditorSnapshot> snapshot =
      m_context.snapshotProvider ? m_context.snapshotProvider() : nullptr;
  if (!snapshot) {
    const json result = MakeError(id, -32001, "Editor snapshot unavailable.");
    finish("snapshot", method, method, id, false, 503, "Editor snapshot unavailable.", &result, {});
    return MakeJsonResponse(503, "Service Unavailable", result);
  }

  if (method == "resources/read") {
    const std::string uri = params.value("uri", std::string());
    if (!IsKnownResourceUri(uri)) {
      const json result = MakeError(id, -32602, "Unknown resource URI.");
      finish("resource", uri, method, id, false, 200, "Unknown resource URI.", &result, {});
      return MakeJsonResponse(200, "OK", result);
    }
    const json result = MakeSuccess(id, BuildResourceReadResult(*snapshot, uri, params));
    finish("resource", uri, method, id, true, 200, std::string(), &result, {});
    return MakeJsonResponse(200, "OK", result);
  }

  if (method == "tools/call") {
    const std::string name = params.value("name", std::string());
    const json arguments = params.value("arguments", json::object());
    activity.requestPreview = TruncatePreview(arguments.dump());
    if (name == "editor.search") {
      const json payloadOut = SearchSnapshot(*snapshot, arguments.value("query", std::string()),
                                            arguments.value("limit", 8),
                                            arguments.value("scope", std::string("all")));
      const json result = MakeSuccess(id, BuildTextToolResult(payloadOut));
      finish("tool", name, method, id, true, 200, std::string(), &result, {});
      return MakeJsonResponse(200, "OK", result);
    }
    if (name == "editor.get_object") {
      const McpObjectSnapshot* object = FindObjectById(*snapshot, arguments.value("id", std::string()));
      if (!object) {
        const json result = MakeError(id, -32602, "Object not found.");
        finish("tool", name, method, id, false, 200, "Object not found.", &result, {});
        return MakeJsonResponse(200, "OK", result);
      }
      const json result = MakeSuccess(id, BuildTextToolResult(BuildObjectJson(*object)));
      finish("tool", name, method, id, true, 200, std::string(), &result, {});
      return MakeJsonResponse(200, "OK", result);
    }
    if (name == "editor.get_object_edges") {
      const McpObjectSnapshot* object = FindObjectById(*snapshot, arguments.value("id", std::string()));
      if (!object) {
        const json result = MakeError(id, -32602, "Object not found.");
        finish("tool", name, method, id, false, 200, "Object not found.", &result, {});
        return MakeJsonResponse(200, "OK", result);
      }
      const json result = MakeSuccess(id, BuildTextToolResult(BuildObjectEdgesJson(*object)));
      finish("tool", name, method, id, true, 200, std::string(), &result, {});
      return MakeJsonResponse(200, "OK", result);
    }
    if (name == "editor.list_objects") {
      const json payloadOut = BuildObjectListJson(*snapshot, arguments.value("limit", 12),
                                                  arguments.value("type", std::string()),
                                                  arguments.value("query", std::string()),
                                                  arguments.value("selectedOnly", false));
      const json result = MakeSuccess(id, BuildTextToolResult(payloadOut));
      finish("tool", name, method, id, true, 200, std::string(), &result, {});
      return MakeJsonResponse(200, "OK", result);
    }
    if (name == "editor.get_objects") {
      json objects = json::array();
      for (const json& item : arguments.value("ids", json::array())) {
        if (!item.is_string())
          continue;
        if (const McpObjectSnapshot* object = FindObjectById(*snapshot, item.get<std::string>()))
          objects.push_back(BuildObjectJson(*object));
      }
      const json result = MakeSuccess(id, BuildTextToolResult(json{{"objects", std::move(objects)}}));
      finish("tool", name, method, id, true, 200, std::string(), &result, {});
      return MakeJsonResponse(200, "OK", result);
    }
    if (name == "editor.get_object_children") {
      const std::string objectId = arguments.value("id", std::string());
      const size_t limit = std::max<size_t>(1, std::min<size_t>(arguments.value("limit", 12), 64));
      json children = json::array();
      size_t totalChildren = 0;
      for (const McpObjectSnapshot& object : snapshot->objects) {
        if (GetParentId(object) != objectId)
          continue;
        ++totalChildren;
        if (children.size() < limit)
          children.push_back(BuildObjectJson(object));
      }
      const json payloadOut = {{"id", objectId},
                               {"children", std::move(children)},
                               {"childCount", totalChildren},
                               {"moreChildren", totalChildren > limit ? totalChildren - limit : 0}};
      const json result = MakeSuccess(id, BuildTextToolResult(payloadOut));
      finish("tool", name, method, id, true, 200, std::string(), &result, {});
      return MakeJsonResponse(200, "OK", result);
    }
    if (name == "editor.get_object_parent") {
      const McpObjectSnapshot* object = FindObjectById(*snapshot, arguments.value("id", std::string()));
      if (!object) {
        const json result = MakeError(id, -32602, "Object not found.");
        finish("tool", name, method, id, false, 200, "Object not found.", &result, {});
        return MakeJsonResponse(200, "OK", result);
      }
      const std::string parentId = GetParentId(*object);
      json payloadOut = {{"id", object->id}, {"parentId", parentId}};
      if (!parentId.empty()) {
        if (const McpObjectSnapshot* parent = FindObjectById(*snapshot, parentId))
          payloadOut["parent"] = BuildObjectJson(*parent);
      }
      const json result = MakeSuccess(id, BuildTextToolResult(payloadOut));
      finish("tool", name, method, id, true, 200, std::string(), &result, {});
      return MakeJsonResponse(200, "OK", result);
    }
    if (name == "editor.count_objects") {
      const std::string typeFilter = ToLowerAscii(arguments.value("type", std::string()));
      const std::string query = arguments.value("query", std::string());
      size_t count = 0;
      for (const McpObjectSnapshot& object : snapshot->objects) {
        if (!typeFilter.empty() && ToLowerAscii(object.type) != typeFilter)
          continue;
        if (!MatchesObjectQuery(object, query))
          continue;
        ++count;
      }
      const json payloadOut = {{"count", count},
                               {"type", arguments.value("type", std::string())},
                               {"query", query}};
      const json result = MakeSuccess(id, BuildTextToolResult(payloadOut));
      finish("tool", name, method, id, true, 200, std::string(), &result, {});
      return MakeJsonResponse(200, "OK", result);
    }
    if (name == "editor.list_assets") {
      const json payloadOut = BuildAssetsCatalogJson(*snapshot, arguments.value("limit", 12),
                                                     arguments.value("query", std::string()));
      const json result = MakeSuccess(id, BuildTextToolResult(payloadOut));
      finish("tool", name, method, id, true, 200, std::string(), &result, {});
      return MakeJsonResponse(200, "OK", result);
    }
    if (name == "editor.get_asset") {
      const std::string assetId = arguments.value("id", std::string());
      const McpAssetSnapshot* asset = FindAssetById(*snapshot, assetId);
      if (!asset) {
        const json result = MakeError(id, -32602, "Asset not found.");
        finish("tool", name, method, id, false, 200, "Asset not found.", &result, {});
        return MakeJsonResponse(200, "OK", result);
      }
      size_t objectReferenceCount = 0;
      for (const McpObjectSnapshot& object : snapshot->objects) {
        if (object.assetId == assetId)
          ++objectReferenceCount;
      }
      json payloadOut = BuildAssetJson(*asset);
      payloadOut["objectReferenceCount"] = objectReferenceCount;
      const json result = MakeSuccess(id, BuildTextToolResult(payloadOut));
      finish("tool", name, method, id, true, 200, std::string(), &result, {});
      return MakeJsonResponse(200, "OK", result);
    }
    if (name == "editor.search_assets") {
      const json payloadOut = SearchAssetsSnapshot(*snapshot, arguments.value("query", std::string()),
                                                   arguments.value("limit", 8));
      const json result = MakeSuccess(id, BuildTextToolResult(payloadOut));
      finish("tool", name, method, id, true, 200, std::string(), &result, {});
      return MakeJsonResponse(200, "OK", result);
    }
    if (name == "editor.count_assets") {
      const std::string query = arguments.value("query", std::string());
      const json payloadOut = BuildAssetsCatalogJson(*snapshot, snapshot->assets.size(), query);
      const json result = MakeSuccess(
          id, BuildTextToolResult(json{{"count", payloadOut.value("matchedAssets", 0U)}, {"query", query}}));
      finish("tool", name, method, id, true, 200, std::string(), &result, {});
      return MakeJsonResponse(200, "OK", result);
    }
    if (name == "editor.scene_status") {
      const json result = MakeSuccess(id, BuildTextToolResult(BuildSceneStatusJson(*snapshot)));
      finish("tool", name, method, id, true, 200, std::string(), &result, {});
      return MakeJsonResponse(200, "OK", result);
    }
    if (name == "editor.get_scene_file") {
      const json payloadOut = {{"filePath", snapshot->sceneFilePath},
                               {"sceneId", snapshot->sceneId},
                               {"sceneName", snapshot->sceneName},
                               {"dirty", snapshot->dirty}};
      const json result = MakeSuccess(id, BuildTextToolResult(payloadOut));
      finish("tool", name, method, id, true, 200, std::string(), &result, {});
      return MakeJsonResponse(200, "OK", result);
    }
    if (name == "editor.search_console") {
      const json payloadOut = SearchConsoleSnapshot(*snapshot, arguments.value("query", std::string()),
                                                    arguments.value("limit", 8));
      const json result = MakeSuccess(id, BuildTextToolResult(payloadOut));
      finish("tool", name, method, id, true, 200, std::string(), &result, {});
      return MakeJsonResponse(200, "OK", result);
    }

    const bool isWriteTool =
        name == "editor.select" || name == "editor.clear_selection" || name == "editor.create_object" ||
        name == "editor.create_object_from_asset" || name == "editor.update_object" ||
        name == "editor.transform" || name == "editor.rename_object" || name == "editor.reparent_object" ||
        name == "editor.duplicate" || name == "editor.delete" || name == "editor.select_asset" ||
        name == "editor.update_asset" || name == "editor.delete_asset" || name == "editor.new_scene" ||
        name == "editor.save_scene" || name == "editor.reload_scene";
    if (!isWriteTool) {
      const json result = MakeError(id, -32601, "Unknown tool.");
      finish("tool", name, method, id, false, 200, "Unknown tool.", &result, {});
      return MakeJsonResponse(200, "OK", result);
    }
    if (!m_context.commandInvoker) {
      const json result = MakeError(id, -32002, "Command queue unavailable.");
      finish("tool", name, method, id, false, 503, "Command queue unavailable.", &result, {});
      return MakeJsonResponse(503, "Service Unavailable", result);
    }

    const McpCommandResult commandResult = m_context.commandInvoker(name, arguments);
    if (!commandResult.ok) {
      const json result = MakeError(id, -32010, commandResult.error);
      finish("tool", name, method, id, false, 200, commandResult.error, &result, {});
      return MakeJsonResponse(200, "OK", result);
    }
    const json result = MakeSuccess(id, BuildTextToolResult(commandResult.data));
    finish("tool", name, method, id, true, 200, std::string(), &result, {});
    return MakeJsonResponse(200, "OK", result);
  }

  const json result = MakeError(id, -32601, "Method not found.");
  finish("method", method, method, id, false, 200, "Method not found.", &result, {});
  return MakeJsonResponse(200, "OK", result);
}

}  // namespace Mcp
}  // namespace Monolith
