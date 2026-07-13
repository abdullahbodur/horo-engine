#include "Horo/Editor/WorkspaceLayoutPersistence.h"

#include <charconv>
#include <fstream>
#include <sstream>
#include <system_error>

namespace Horo::Editor {
    namespace {
        class Parser {
        public:
            explicit Parser(const std::string_view text) : m_text(text) {
            }

            std::optional<WorkspaceLayout> Parse(std::string &error) {
                WorkspaceLayout layout;
                Skip();
                if (!ObjectStart() || !Key("schemaVersion") || !UInt(layout.schemaVersion) || !Comma() || !
                    Key("root")) {
                    error = "invalid workspace document header";
                    return std::nullopt;
                }
                const auto root = Node(error);
                if (!root || !ObjectEnd()) return std::nullopt;
                layout.root = std::move(*root);
                if (layout.schemaVersion != WorkspaceLayoutPersistence::CurrentSchemaVersion) {
                    error = "unsupported workspace schema version";
                    return std::nullopt;
                }
                if (!layout.Validate().empty()) return std::nullopt;
                return layout;
            }

        private:
            std::string_view m_text;
            std::size_t m_pos = 0;
            WorkspaceLayout m_layout;

            void Skip() {
                while (m_pos < m_text.size() && (m_text[m_pos] == ' ' || m_text[m_pos] == '\n' || m_text[m_pos] == '\r'
                                                 || m_text[m_pos] == '\t')) ++m_pos;
            }

            bool Take(const char c) {
                Skip();
                if (m_pos >= m_text.size() || m_text[m_pos] != c) return false;
                ++m_pos;
                return true;
            }

            bool ObjectStart() { return Take('{'); }
            bool ObjectEnd() { return Take('}'); }
            bool Comma() { return Take(','); }

            bool Key(const char *key) {
                std::string value;
                return String(value) && Take(':') && value == key;
            }

            bool String(std::string &out) {
                Skip();
                if (m_pos >= m_text.size() || m_text[m_pos++] != '"') return false;
                out.clear();
                while (m_pos < m_text.size()) {
                    const char c = m_text[m_pos++];
                    if (c == '"') return true;
                    if (c == '\\' && m_pos < m_text.size()) {
                        const char escaped = m_text[m_pos++];
                        out += escaped == 'n' ? '\n' : escaped;
                    } else out += c;
                }
                return false;
            }

            bool UInt(std::uint32_t &out) {
                Skip();
                const char *begin = m_text.data() + m_pos;
                const char *end = m_text.data() + m_text.size();
                auto [ptr, ec] = std::from_chars(begin, end, out);
                if (ec != std::errc{}) return false;
                m_pos = static_cast<std::size_t>(ptr - m_text.data());
                return true;
            }

            bool Float(float &out) {
                Skip();
                const std::size_t start = m_pos;
                while (m_pos < m_text.size() && (m_text[m_pos] == '-' || m_text[m_pos] == '.' ||
                                                 (m_text[m_pos] >= '0' && m_text[m_pos] <= '9')))
                    ++m_pos;
                try {
                    out = std::stof(std::string(m_text.substr(start, m_pos - start)));
                    return true;
                } catch (...) { return false; }
            }

            std::optional<LayoutNode> Node(std::string &error) {
                if (!ObjectStart()) {
                    error = "node must be an object";
                    return std::nullopt;
                }
                if (!Key("type")) {
                    error = "node type missing";
                    return std::nullopt;
                }
                std::string type;
                if (!String(type) || !Comma() || !Key("id")) {
                    error = "node identity missing";
                    return std::nullopt;
                }
                std::string id;
                if (!String(id) || !Comma()) {
                    error = "node id invalid";
                    return std::nullopt;
                }
                if (type == "panel") {
                    if (!Key("panel")) {
                        error = "panel identity missing";
                        return std::nullopt;
                    }
                    std::string panel;
                    if (!String(panel) || !ObjectEnd()) {
                        error = "panel node invalid";
                        return std::nullopt;
                    }
                    return LayoutNode(PanelNode{std::move(id), std::move(panel)});
                }
                if (type == "stack") {
                    if (!Key("tabs")) {
                        error = "stack tabs missing";
                        return std::nullopt;
                    }
                    if (!Take('[')) {
                        error = "stack tabs invalid";
                        return std::nullopt;
                    }
                    TabStackNode stack{std::move(id)};
                    Skip();
                    if (!Take(']')) {
                        while (true) {
                            std::string tab;
                            if (!String(tab)) {
                                error = "tab invalid";
                                return std::nullopt;
                            }
                            stack.tabs.push_back(std::move(tab));
                            if (Take(']')) break;
                            if (!Comma()) {
                                error = "tab separator missing";
                                return std::nullopt;
                            }
                        }
                    }
                    if (!Comma() || !Key("active") || !String(stack.activeTab.emplace()) || !ObjectEnd()) {
                        error = "stack active tab invalid";
                        return std::nullopt;
                    }
                    return LayoutNode(std::move(stack));
                }
                if (type != "split" || !Key("axis")) {
                    error = "unknown node type";
                    return std::nullopt;
                }
                std::string axis;
                float ratio = 0.5F;
                if (!String(axis) || !Comma() || !Key("ratio") || !Float(ratio) || !Comma() || !Key("first")) {
                    error = "split properties invalid";
                    return std::nullopt;
                }
                const auto first = Node(error);
                if (!first || !Comma() || !Key("second")) return std::nullopt;
                const auto second = Node(error);
                if (!second || !ObjectEnd()) return std::nullopt;
                return LayoutNode(SplitNode{
                    std::move(id), axis == "vertical" ? WorkspaceSplitAxis::Vertical : WorkspaceSplitAxis::Horizontal,
                    ratio, 160.0F, 160.0F, std::make_unique<LayoutNode>(std::move(*first)),
                    std::make_unique<LayoutNode>(std::move(*second))
                });
            }
        };

        std::string Escape(const std::string_view value) {
            std::string out;
            for (const char c: value) {
                if (c == '"' || c == '\\') out += '\\';
                out += c;
            }
            return out;
        }

        void WriteNode(std::ostringstream &out, const LayoutNode &node) {
            std::visit([&](const auto &value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, PanelNode>)
                    out << "{\"type\":\"panel\",\"id\":\"" << Escape(value.id) << "\",\"panel\":\"" << Escape(
                        value.panel) << "\"}";
                else if constexpr (std::is_same_v<T, TabStackNode>) {
                    out << "{\"type\":\"stack\",\"id\":\"" << Escape(value.id) << "\",\"tabs\":[";
                    for (std::size_t i = 0; i < value.tabs.size(); ++i) {
                        if (i) out << ',';
                        out << '\"' << Escape(value.tabs[i]) << '\"';
                    }
                    out << "],\"active\":\"" << Escape(value.activeTab.value_or("")) << "\"}";
                } else {
                    out << "{\"type\":\"split\",\"id\":\"" << Escape(value.id) << "\",\"axis\":\"" << (
                                value.axis == WorkspaceSplitAxis::Vertical ? "vertical" : "horizontal") <<
                            "\",\"ratio\":" <<
                            value.ratio << ",\"first\":";
                    WriteNode(out, *value.first);
                    out << ",\"second\":";
                    WriteNode(out, *value.second);
                    out << '}';
                }
            }, node.value);
        }
    }

    std::string WorkspaceLayoutPersistence::Serialize(const WorkspaceLayout &layout) {
        std::ostringstream out;
        out << "{\"schemaVersion\":" << CurrentSchemaVersion << ",\"root\":";
        WriteNode(out, layout.root);
        out << '}';
        return out.str();
    }

    std::optional<WorkspaceLayout> WorkspaceLayoutPersistence::Deserialize(
        const std::string_view json, std::string *error) {
        std::string local;
        Parser parser(json);
        auto result = parser.Parse(local);
        if (!result && error) *error = local;
        return result;
    }

    bool WorkspaceLayoutPersistence::Save(const std::filesystem::path &path, const WorkspaceLayout &layout,
                                          std::string *error) {
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) {
            if (error)*error = ec.message();
            return false;
        }
        const auto temp = path.string() + ".tmp";
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        out << Serialize(layout);
        out.close();
        if (!out) {
            if (error)*error = "workspace write failed";
            return false;
        }
        std::filesystem::rename(temp, path, ec);
        if (ec) {
            std::filesystem::remove(path, ec);
            std::filesystem::rename(temp, path, ec);
        }
        if (ec && error) *error = ec.message();
        return !ec;
    }

    std::optional<WorkspaceLayout> WorkspaceLayoutPersistence::Load(const std::filesystem::path &path,
                                                                    std::string *error) {
        const std::ifstream in(path, std::ios::binary);
        if (!in) {
            if (error)*error = "workspace file not found";
            return std::nullopt;
        }
        std::stringstream buffer;
        buffer << in.rdbuf();
        return Deserialize(buffer.str(), error);
    }
} // namespace Horo::Editor
