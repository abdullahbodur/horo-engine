#include "editor/BlueprintSnapshot.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace Monolith {
namespace Editor {
namespace {

// ---- Color palette --------------------------------------------------------

struct Color
{
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

static constexpr Color kBackground{7, 27, 44};
static constexpr Color kGrid{24, 60, 86};
static constexpr Color kFloor{20, 88, 122};
static constexpr Color kWall{190, 235, 255};
static constexpr Color kProp{255, 196, 92};
static constexpr Color kLight{255, 120, 80};
static constexpr Color kAxis{120, 200, 255};

// ---- Image ----------------------------------------------------------------

class Image
{
public:
    Image(int w, int h, Color fill) : m_width(w), m_height(h), m_pixels(static_cast<size_t>(w * h), fill) {}

    int Width() const { return m_width; }
    int Height() const { return m_height; }

    void SetPixel(int x, int y, Color c)
    {
        if (x < 0 || y < 0 || x >= m_width || y >= m_height)
            return;
        m_pixels[static_cast<size_t>(y * m_width + x)] = c;
    }

    void FillRect(int x0, int y0, int x1, int y1, Color c)
    {
        if (x0 > x1)
            std::swap(x0, x1);
        if (y0 > y1)
            std::swap(y0, y1);
        x0 = std::max(0, x0);
        y0 = std::max(0, y0);
        x1 = std::min(m_width - 1, x1);
        y1 = std::min(m_height - 1, y1);
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x)
                SetPixel(x, y, c);
    }

    void StrokeRect(int x0, int y0, int x1, int y1, Color c, int thickness = 1)
    {
        for (int t = 0; t < thickness; ++t)
        {
            FillRect(x0 + t, y0 + t, x1 - t, y0 + t, c);
            FillRect(x0 + t, y1 - t, x1 - t, y1 - t, c);
            FillRect(x0 + t, y0 + t, x0 + t, y1 - t, c);
            FillRect(x1 - t, y0 + t, x1 - t, y1 - t, c);
        }
    }

    void FillCircle(int cx, int cy, int radius, Color c)
    {
        for (int y = -radius; y <= radius; ++y)
            for (int x = -radius; x <= radius; ++x)
                if (x * x + y * y <= radius * radius)
                    SetPixel(cx + x, cy + y, c);
    }

    void DrawLine(int x0, int y0, int x1, int y1, Color c)
    {
        int dx = std::abs(x1 - x0);
        int sx = x0 < x1 ? 1 : -1;
        int dy = -std::abs(y1 - y0);
        int sy = y0 < y1 ? 1 : -1;
        int err = dx + dy;
        while (true)
        {
            SetPixel(x0, y0, c);
            if (x0 == x1 && y0 == y1)
                break;
            int e2 = 2 * err;
            if (e2 >= dy)
            {
                err += dy;
                x0 += sx;
            }
            if (e2 <= dx)
            {
                err += dx;
                y0 += sy;
            }
        }
    }

    void SavePpm(const std::string& path) const
    {
        std::ofstream out(path, std::ios::binary);
        if (!out.is_open())
            throw std::runtime_error("BlueprintSnapshot: cannot write '" + path + "'");
        out << "P6\n" << m_width << ' ' << m_height << "\n255\n";
        for (const auto& px : m_pixels)
        {
            out.write(reinterpret_cast<const char*>(&px.r), 1);
            out.write(reinterpret_cast<const char*>(&px.g), 1);
            out.write(reinterpret_cast<const char*>(&px.b), 1);
        }
    }

private:
    int m_width;
    int m_height;
    std::vector<Color> m_pixels;
};

// ---- Viewport -------------------------------------------------------------

struct Viewport
{
    float minX = 0.0f;
    float maxX = 1.0f;
    float minZ = 0.0f;
    float maxZ = 1.0f;
    int width = 1;
    int height = 1;
    int padding = 0;
    float pixelsPerUnit = 1.0f;
};

// ---- Helpers --------------------------------------------------------------

static float SafeHalf(float value, float fallback = 0.35f)
{
    return value > 0.001f ? value : fallback;
}

static void ExpandBounds(BlueprintSnapshotAnalysis& a, float minX, float maxX, float minZ, float maxZ)
{
    a.min.x = std::min(a.min.x, minX);
    a.max.x = std::max(a.max.x, maxX);
    a.min.z = std::min(a.min.z, minZ);
    a.max.z = std::max(a.max.z, maxZ);
}

// Panels whose Y half-extent is <= 0.25 units are treated as floors.
static bool IsFloor(const SceneObject& obj)
{
    return obj.scale.y <= 0.25f;
}

static std::array<int, 4> ProjectRect(const Viewport& vp, float minX, float maxX, float minZ, float maxZ)
{
    auto mapX = [&](float x) {
        return vp.padding + static_cast<int>(std::lround((x - vp.minX) * vp.pixelsPerUnit));
    };
    auto mapY = [&](float z) {
        return vp.height - vp.padding - static_cast<int>(std::lround((z - vp.minZ) * vp.pixelsPerUnit));
    };
    return {mapX(minX), mapY(maxZ), mapX(maxX), mapY(minZ)};
}

static Viewport MakeViewport(const BlueprintSnapshotAnalysis& analysis, const BlueprintSnapshotOptions& opts)
{
    float worldW = std::max(analysis.max.x - analysis.min.x, 1.0f);
    float worldH = std::max(analysis.max.z - analysis.min.z, 1.0f);
    float usable = static_cast<float>(std::max(1, opts.imageSize - opts.padding * 2));
    float scale = usable / std::max(worldW, worldH);

    float extraX = (usable / scale - worldW) * 0.5f;
    float extraZ = (usable / scale - worldH) * 0.5f;

    Viewport vp;
    vp.minX = analysis.min.x - extraX;
    vp.maxX = analysis.max.x + extraX;
    vp.minZ = analysis.min.z - extraZ;
    vp.maxZ = analysis.max.z + extraZ;
    vp.width = opts.imageSize;
    vp.height = opts.imageSize;
    vp.padding = opts.padding;
    vp.pixelsPerUnit = scale;
    return vp;
}

// ---- SVG output -----------------------------------------------------------

static std::string ColorToSvg(Color c)
{
    std::ostringstream ss;
    ss << "rgb(" << static_cast<int>(c.r) << ',' << static_cast<int>(c.g) << ',' << static_cast<int>(c.b) << ')';
    return ss.str();
}

static std::string EscapeXml(const std::string& value)
{
    std::string out;
    out.reserve(value.size());
    for (char ch : value)
    {
        switch (ch)
        {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;";  break;
            case '>': out += "&gt;";  break;
            case '"': out += "&quot;"; break;
            default:  out.push_back(ch); break;
        }
    }
    return out;
}

static void WriteSvg(const SceneDocument& doc,
                     const BlueprintSnapshotResult& result,
                     const Viewport& vp)
{
    std::ofstream out(result.svgPath);
    if (!out.is_open())
        throw std::runtime_error("BlueprintSnapshot: cannot write '" + result.svgPath + "'");

    out << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << result.width
        << "\" height=\"" << result.height
        << "\" viewBox=\"0 0 " << result.width << ' ' << result.height << "\">\n";
    out << "  <rect width=\"100%\" height=\"100%\" fill=\"" << ColorToSvg(kBackground) << "\"/>\n";

    // Grid
    int startX = static_cast<int>(std::floor(vp.minX));
    int endX   = static_cast<int>(std::ceil(vp.maxX));
    int startZ = static_cast<int>(std::floor(vp.minZ));
    int endZ   = static_cast<int>(std::ceil(vp.maxZ));

    out << "  <g stroke=\"" << ColorToSvg(kGrid) << "\" stroke-width=\"1\" opacity=\"0.9\">\n";
    for (int x = startX; x <= endX; ++x)
    {
        auto p = ProjectRect(vp, static_cast<float>(x), static_cast<float>(x), vp.minZ, vp.maxZ);
        out << "    <line x1=\"" << p[0] << "\" y1=\"" << p[1]
            << "\" x2=\"" << p[2] << "\" y2=\"" << p[3] << "\"/>\n";
    }
    for (int z = startZ; z <= endZ; ++z)
    {
        auto p = ProjectRect(vp, vp.minX, vp.maxX, static_cast<float>(z), static_cast<float>(z));
        out << "    <line x1=\"" << p[0] << "\" y1=\"" << p[1]
            << "\" x2=\"" << p[2] << "\" y2=\"" << p[3] << "\"/>\n";
    }
    out << "  </g>\n";

    // Floor panels
    out << "  <g fill=\"" << ColorToSvg(kFloor) << "\">\n";
    for (const auto& obj : doc.objects)
    {
        if (obj.type != SceneObjectType::Panel || !IsFloor(obj))
            continue;
        auto r = ProjectRect(vp,
                             obj.position.x - obj.scale.x, obj.position.x + obj.scale.x,
                             obj.position.z - obj.scale.z, obj.position.z + obj.scale.z);
        out << "    <rect x=\"" << r[0] << "\" y=\"" << r[1]
            << "\" width=\"" << (r[2] - r[0]) << "\" height=\"" << (r[3] - r[1])
            << "\" opacity=\"0.85\"/>\n";
    }
    out << "  </g>\n";

    // Wall panels
    out << "  <g fill=\"" << ColorToSvg(kWall) << "\">\n";
    for (const auto& obj : doc.objects)
    {
        if (obj.type != SceneObjectType::Panel || IsFloor(obj))
            continue;
        auto r = ProjectRect(vp,
                             obj.position.x - obj.scale.x, obj.position.x + obj.scale.x,
                             obj.position.z - obj.scale.z, obj.position.z + obj.scale.z);
        out << "    <rect x=\"" << r[0] << "\" y=\"" << r[1]
            << "\" width=\"" << (r[2] - r[0]) << "\" height=\"" << (r[3] - r[1])
            << "\" opacity=\"0.95\"/>\n";
    }
    out << "  </g>\n";

    // Props
    out << "  <g fill=\"" << ColorToSvg(kProp) << "\">\n";
    for (const auto& obj : doc.objects)
    {
        if (obj.type != SceneObjectType::Prop)
            continue;
        float hx = SafeHalf(obj.scale.x);
        float hz = SafeHalf(obj.scale.z);
        auto r = ProjectRect(vp,
                             obj.position.x - hx, obj.position.x + hx,
                             obj.position.z - hz, obj.position.z + hz);
        out << "    <rect x=\"" << r[0] << "\" y=\"" << r[1]
            << "\" width=\"" << (r[2] - r[0]) << "\" height=\"" << (r[3] - r[1])
            << "\" opacity=\"0.9\"/>\n";
    }
    out << "  </g>\n";

    // Lights
    out << "  <g fill=\"" << ColorToSvg(kLight) << "\">\n";
    for (const auto& obj : doc.objects)
    {
        if (obj.type != SceneObjectType::Light)
            continue;
        auto p = ProjectRect(vp, obj.position.x, obj.position.x, obj.position.z, obj.position.z);
        out << "    <circle cx=\"" << p[0] << "\" cy=\"" << p[1] << "\" r=\"6\" opacity=\"0.9\"/>\n";
    }
    out << "  </g>\n";

    // Origin cross
    auto origin = ProjectRect(vp, 0.0f, 0.0f, 0.0f, 0.0f);
    out << "  <g stroke=\"" << ColorToSvg(kAxis) << "\" stroke-width=\"2\">\n";
    out << "    <line x1=\"" << origin[0] - 10 << "\" y1=\"" << origin[1]
        << "\" x2=\"" << origin[0] + 10 << "\" y2=\"" << origin[1] << "\"/>\n";
    out << "    <line x1=\"" << origin[0] << "\" y1=\"" << origin[1] - 10
        << "\" x2=\"" << origin[0] << "\" y2=\"" << origin[1] + 10 << "\"/>\n";
    out << "  </g>\n";

    // Metadata
    out << "  <g font-family=\"monospace\" font-size=\"18\" fill=\"" << ColorToSvg(kWall) << "\">\n";
    out << "    <text x=\"24\" y=\"30\">Monolith blueprint snapshot</text>\n";
    out << "    <text x=\"24\" y=\"54\">Source: " << EscapeXml(result.sourceDescription) << "</text>\n";
    out << "    <text x=\"24\" y=\"78\">Panels: " << result.analysis.panelCount
        << " | Props: " << result.analysis.propCount
        << " | Lights: " << result.analysis.lightCount << "</text>\n";
    out << "  </g>\n";
    out << "</svg>\n";
}

}  // namespace

// ---- BlueprintSnapshot public API -----------------------------------------

BlueprintSnapshotAnalysis BlueprintSnapshot::Analyze(const SceneDocument& doc)
{
    BlueprintSnapshotAnalysis a;
    a.min = {1e9f, 0.0f, 1e9f};
    a.max = {-1e9f, 0.0f, -1e9f};

    for (const auto& obj : doc.objects)
    {
        switch (obj.type)
        {
            case SceneObjectType::Panel:
            {
                ++a.panelCount;
                ExpandBounds(a,
                             obj.position.x - obj.scale.x, obj.position.x + obj.scale.x,
                             obj.position.z - obj.scale.z, obj.position.z + obj.scale.z);
                break;
            }
            case SceneObjectType::Prop:
            {
                ++a.propCount;
                float hx = SafeHalf(obj.scale.x);
                float hz = SafeHalf(obj.scale.z);
                ExpandBounds(a,
                             obj.position.x - hx, obj.position.x + hx,
                             obj.position.z - hz, obj.position.z + hz);
                break;
            }
            case SceneObjectType::Light:
            {
                ++a.lightCount;
                ExpandBounds(a,
                             obj.position.x - 0.25f, obj.position.x + 0.25f,
                             obj.position.z - 0.25f, obj.position.z + 0.25f);
                break;
            }
        }
    }

    if (a.panelCount == 0 && a.propCount == 0 && a.lightCount == 0)
    {
        a.min = {-1.0f, 0.0f, -1.0f};
        a.max = {1.0f, 0.0f, 1.0f};
    }

    return a;
}

BlueprintSnapshotResult BlueprintSnapshot::Generate(const SceneDocument& doc,
                                                    const BlueprintSnapshotOptions& opts)
{
    BlueprintSnapshotResult result;
    result.analysis = Analyze(doc);
    result.sourceDescription = doc.filePath.empty() ? ("scene:" + doc.sceneId) : doc.filePath;
    result.width = opts.imageSize;
    result.height = opts.imageSize;
    result.ppmPath = opts.outputBasePath + ".ppm";
    result.svgPath = opts.outputBasePath + ".svg";

    auto ensureDir = [](const std::string& path) {
        auto parent = std::filesystem::path(path).parent_path();
        if (!parent.empty())
            std::filesystem::create_directories(parent);
    };
    ensureDir(result.ppmPath);
    ensureDir(result.svgPath);

    Viewport vp = MakeViewport(result.analysis, opts);

    Image image(result.width, result.height, kBackground);

    // Grid lines
    int startX = static_cast<int>(std::floor(vp.minX));
    int endX   = static_cast<int>(std::ceil(vp.maxX));
    int startZ = static_cast<int>(std::floor(vp.minZ));
    int endZ   = static_cast<int>(std::ceil(vp.maxZ));
    for (int x = startX; x <= endX; ++x)
    {
        auto p = ProjectRect(vp, static_cast<float>(x), static_cast<float>(x), vp.minZ, vp.maxZ);
        image.DrawLine(p[0], p[1], p[2], p[3], kGrid);
    }
    for (int z = startZ; z <= endZ; ++z)
    {
        auto p = ProjectRect(vp, vp.minX, vp.maxX, static_cast<float>(z), static_cast<float>(z));
        image.DrawLine(p[0], p[1], p[2], p[3], kGrid);
    }

    // Floor panels
    for (const auto& obj : doc.objects)
    {
        if (obj.type != SceneObjectType::Panel || !IsFloor(obj))
            continue;
        auto r = ProjectRect(vp,
                             obj.position.x - obj.scale.x, obj.position.x + obj.scale.x,
                             obj.position.z - obj.scale.z, obj.position.z + obj.scale.z);
        image.FillRect(r[0], r[1], r[2], r[3], kFloor);
    }

    // Wall panels
    for (const auto& obj : doc.objects)
    {
        if (obj.type != SceneObjectType::Panel || IsFloor(obj))
            continue;
        auto r = ProjectRect(vp,
                             obj.position.x - obj.scale.x, obj.position.x + obj.scale.x,
                             obj.position.z - obj.scale.z, obj.position.z + obj.scale.z);
        image.FillRect(r[0], r[1], r[2], r[3], kWall);
        image.StrokeRect(r[0], r[1], r[2], r[3], kBackground, 1);
    }

    // Props
    for (const auto& obj : doc.objects)
    {
        if (obj.type != SceneObjectType::Prop)
            continue;
        float hx = SafeHalf(obj.scale.x);
        float hz = SafeHalf(obj.scale.z);
        auto r = ProjectRect(vp,
                             obj.position.x - hx, obj.position.x + hx,
                             obj.position.z - hz, obj.position.z + hz);
        image.FillRect(r[0], r[1], r[2], r[3], kProp);
        image.StrokeRect(r[0], r[1], r[2], r[3], kBackground, 1);
    }

    // Lights
    for (const auto& obj : doc.objects)
    {
        if (obj.type != SceneObjectType::Light)
            continue;
        auto p = ProjectRect(vp, obj.position.x, obj.position.x, obj.position.z, obj.position.z);
        image.FillCircle(p[0], p[1], 5, kLight);
    }

    // Origin cross
    auto origin = ProjectRect(vp, 0.0f, 0.0f, 0.0f, 0.0f);
    image.DrawLine(origin[0] - 10, origin[1], origin[0] + 10, origin[1], kAxis);
    image.DrawLine(origin[0], origin[1] - 10, origin[0], origin[1] + 10, kAxis);

    image.SavePpm(result.ppmPath);
    WriteSvg(doc, result, vp);
    return result;
}

}  // namespace Editor
}  // namespace Monolith
