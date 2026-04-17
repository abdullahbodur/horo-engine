#include "renderer/ObjLoader.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "math/Vec2.h"

namespace Monolith {

namespace ObjLoader {

// Simple .obj parser: v, vt, vn, f (triangulated) lines only.
// Face indices are 1-based and may be v, v/vt, v//vn, or v/vt/vn.
Mesh Load(const std::string& path) {
  std::ifstream file(path);
  if (!file.is_open())
    throw ObjLoaderException("ObjLoader: cannot open '" + path + "'");

  std::vector<Vec3> positions;
  std::vector<Vec3> normals;
  std::vector<Vec2> uvs;

  std::vector<Vertex> vertices;
  std::vector<unsigned int> indices;

  auto parseFaceVertex = [&](const std::string& token) -> Vertex {
    Vertex v{};
    int pi = 0, ti = 0, ni = 0;

    // Split by '/'
    size_t s1 = token.find('/');
    if (s1 == std::string::npos) {
      pi = std::stoi(token);
    } else {
      pi = std::stoi(token.substr(0, s1));
      size_t s2 = token.find('/', s1 + 1);
      if (s2 == std::string::npos) {
        ti = std::stoi(token.substr(s1 + 1));
      } else {
        if (s2 > s1 + 1)
          ti = std::stoi(token.substr(s1 + 1, s2 - s1 - 1));
        if (s2 + 1 < token.size())
          ni = std::stoi(token.substr(s2 + 1));
      }
    }

    if (pi != 0) {
      int idx = pi > 0 ? pi - 1 : (int)positions.size() + pi;
      if (idx >= 0 && idx < (int)positions.size())
        v.position = positions[static_cast<size_t>(idx)];
    }
    if (ti != 0) {
      int idx = ti > 0 ? ti - 1 : (int)uvs.size() + ti;
      if (idx >= 0 && idx < (int)uvs.size())
        v.uv = uvs[static_cast<size_t>(idx)];
    }
    if (ni != 0) {
      int idx = ni > 0 ? ni - 1 : (int)normals.size() + ni;
      if (idx >= 0 && idx < (int)normals.size())
        v.normal = normals[static_cast<size_t>(idx)];
    }
    return v;
  };

  std::string line;
  while (std::getline(file, line)) {
    if (line.empty() || line[0] == '#')
      continue;

    std::istringstream ss(line);
    std::string keyword;
    ss >> keyword;

    if (keyword == "v") {
      float x, y, z;
      ss >> x >> y >> z;
      positions.push_back({x, y, z});
    } else if (keyword == "vn") {
      float x, y, z;
      ss >> x >> y >> z;
      normals.push_back({x, y, z});
    } else if (keyword == "vt") {
      float u, v;
      ss >> u >> v;
      uvs.push_back({u, v});
    } else if (keyword == "f") {
      // Fan triangulate polygon
      std::vector<std::string> tokens;
      std::string tok;
      while (ss >> tok)
        tokens.push_back(tok);

      if (tokens.size() < 3)
        continue;

      Vertex v0 = parseFaceVertex(tokens[0]);
      for (size_t i = 1; i + 1 < tokens.size(); ++i) {
        Vertex v1 = parseFaceVertex(tokens[i]);
        Vertex v2 = parseFaceVertex(tokens[i + 1]);

        unsigned int base = static_cast<unsigned int>(vertices.size());
        vertices.push_back(v0);
        vertices.push_back(v1);
        vertices.push_back(v2);
        indices.push_back(base);
        indices.push_back(base + 1);
        indices.push_back(base + 2);
      }
    }
  }

  // Auto-generate flat normals for meshes that shipped without vn lines
  if (normals.empty()) {
    for (size_t i = 0; i + 2 < vertices.size(); i += 3) {
      Vec3 e1 = vertices[i + 1].position - vertices[i].position;
      Vec3 e2 = vertices[i + 2].position - vertices[i].position;
      Vec3 n = Vec3::Cross(e1, e2).Normalized();
      vertices[i].normal = n;
      vertices[i + 1].normal = n;
      vertices[i + 2].normal = n;
    }
  }

  if (vertices.empty())
    throw ObjLoaderException("ObjLoader: no geometry in '" + path + "'");

  Mesh mesh;
  mesh.SetData(vertices, indices);
  return mesh;
}

ObjAABB ComputeAABB(const std::string& path)
{
    ObjAABB result;
    std::ifstream file(path);
    if (!file.is_open())
        return result;

    std::string line;
    while (std::getline(file, line)) {
        if (line.size() < 2 || line[0] != 'v' || line[1] != ' ')
            continue;
        std::istringstream ss(line);
        std::string kw;
        float x, y, z;
        ss >> kw >> x >> y >> z;
        if (ss.fail())
            continue;
        if (!result.valid) {
            result.min = result.max = {x, y, z};
            result.valid = true;
        } else {
            result.min.x = std::min(result.min.x, x);
            result.min.y = std::min(result.min.y, y);
            result.min.z = std::min(result.min.z, z);
            result.max.x = std::max(result.max.x, x);
            result.max.y = std::max(result.max.y, y);
            result.max.z = std::max(result.max.z, z);
        }
    }
    return result;
}

std::string FindDiffuseTexture(const std::string& objPath)
{
    // 1. Read the OBJ to find "mtllib <name>"
    std::ifstream objFile(objPath);
    if (!objFile.is_open())
        return {};

    std::string mtlName;
    std::string line;
    while (std::getline(objFile, line))
    {
        if (line.rfind("mtllib ", 0) == 0)
        {
            mtlName = line.substr(7);
            // Trim trailing whitespace/CR
            while (!mtlName.empty() && (mtlName.back() == '\r' || mtlName.back() == ' '))
                mtlName.pop_back();
            break;
        }
    }
    if (mtlName.empty())
        return {};

    // 2. Resolve MTL path relative to the OBJ directory
    namespace fs = std::filesystem;
    const fs::path objDir = fs::path(objPath).parent_path();
    const std::string mtlPath = (objDir / mtlName).string();

    std::ifstream mtlFile(mtlPath);
    if (!mtlFile.is_open())
        return {};

    // 3. Find "map_Kd <texture>" in the MTL
    std::string texName;
    while (std::getline(mtlFile, line))
    {
        // map_Kd may appear as "map_Kd" or "map_kd" (case-insensitive)
        if (line.size() < 7)
            continue;
        std::string prefix = line.substr(0, 7);
        std::transform(prefix.begin(), prefix.end(), prefix.begin(),
                       [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
        if (prefix == "map_kd ")
        {
            texName = line.substr(7);
            while (!texName.empty() && (texName.back() == '\r' || texName.back() == ' '))
                texName.pop_back();
            break;
        }
    }
    if (texName.empty())
        return {};

    // 4. Resolve texture path relative to MTL directory (same as OBJ dir)
    return (objDir / texName).generic_string();
}

}  // namespace ObjLoader

}  // namespace Monolith
