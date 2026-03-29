#include "renderer/ObjLoader.h"

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
    throw std::runtime_error("ObjLoader: cannot open '" + path + "'");

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

  if (vertices.empty())
    throw std::runtime_error("ObjLoader: no geometry in '" + path + "'");

  Mesh mesh;
  mesh.SetData(vertices, indices);
  return mesh;
}

}  // namespace ObjLoader

}  // namespace Monolith
