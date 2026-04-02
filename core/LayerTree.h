/* ============================================================================

   STROVA - 2D ANIMATION ENGINE
   ----------------------------------------------------------------------------
   File:        core/LayerTree.h
   Module:      Core
   Purpose:     Lightweight layer tree metadata and selection helpers.

   Notes:
   - Keeps draw layers separate from non-draw timeline tracks.
   - Header-only so it can be dropped into existing builds easily.

   Copyright (c) 2026 Strova Project
   ============================================================================ */
#pragma once

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace strova
{
    struct TimelineTrack;
    class TimelineWidget;

    struct LayerNode
    {
        int id = 0;
        int parentId = 0;
        int trackId = 0;
        bool isGroup = false;
        bool expanded = true;
        std::string name;
    };

    struct LayerRow
    {
        int nodeId = 0;
        int depth = 0;
    };

    class LayerTree
    {
    public:
        void clear()
        {
            nodes.clear();
            selection.clear();
            nextNodeId = 1;
            anchorNodeId = 0;
        }

        const std::vector<LayerNode>& getNodes() const { return nodes; }
        std::vector<int> getSelection() const { return selection; }

        const LayerNode* findNode(int nodeId) const
        {
            for (const auto& n : nodes) if (n.id == nodeId) return &n;
            return nullptr;
        }

        LayerNode* findNode(int nodeId)
        {
            for (auto& n : nodes) if (n.id == nodeId) return &n;
            return nullptr;
        }

        const LayerNode* findByTrackId(int trackId) const
        {
            for (const auto& n : nodes) if (n.trackId == trackId && !n.isGroup) return &n;
            return nullptr;
        }

        LayerNode* findByTrackId(int trackId)
        {
            for (auto& n : nodes) if (n.trackId == trackId && !n.isGroup) return &n;
            return nullptr;
        }

        template<typename TTimeline>
        void syncFromTimeline(const TTimeline& timeline)
        {
            std::set<int> drawTrackIds;
            for (const auto& t : timeline.state().tracks)
            {
                if ((int)t.kind != 1) continue;
                if (t.engineTrackId == 0) continue;

                drawTrackIds.insert(t.engineTrackId);
                LayerNode* node = findByTrackId(t.engineTrackId);
                if (!node)
                {
                    LayerNode nn;
                    nn.id = nextNodeId++;
                    nn.parentId = 0;
                    nn.trackId = t.engineTrackId;
                    nn.isGroup = false;
                    nn.expanded = true;
                    nn.name = t.name.empty() ? "Layer" : t.name;
                    nodes.push_back(nn);
                }
                else if (!t.name.empty())
                {
                    node->name = t.name;
                }
            }

            cleanupAfterTimelineSync(drawTrackIds);
        }

        template<typename TTimeline>
        void syncExistingFromTimeline(const TTimeline& timeline)
        {
            std::set<int> drawTrackIds;
            for (const auto& t : timeline.state().tracks)
            {
                if ((int)t.kind != 1) continue;
                if (t.engineTrackId == 0) continue;

                drawTrackIds.insert(t.engineTrackId);
                if (LayerNode* node = findByTrackId(t.engineTrackId))
                {
                    if (!t.name.empty())
                        node->name = t.name;
                }
            }

            cleanupAfterTimelineSync(drawTrackIds);
        }

        int firstLayerNodeId() const
        {
            for (const auto& r : buildRows())
            {
                const LayerNode* n = findNode(r.nodeId);
                if (n && !n->isGroup) return n->id;
            }
            return 0;
        }

        int primarySelectedNodeId() const
        {
            return selection.empty() ? 0 : selection.back();
        }

        int primarySelectedTrackId() const
        {
            const LayerNode* n = findNode(primarySelectedNodeId());
            if (!n || n->isGroup) return 0;
            return n->trackId;
        }

        int selectedParentForNewNode() const
        {
            const LayerNode* n = findNode(primarySelectedNodeId());
            if (!n) return 0;
            return n->id;
        }

        std::vector<int> selectedTrackIds() const
        {
            std::vector<int> out;
            for (int id : selection)
            {
                const LayerNode* n = findNode(id);
                if (n && !n->isGroup && n->trackId != 0)
                    out.push_back(n->trackId);
            }
            return out;
        }

        int addLayerNode(const std::string& name, int trackId, int parentId = 0)
        {
            LayerNode n;
            n.id = nextNodeId++;
            n.parentId = parentId;
            n.trackId = trackId;
            n.isGroup = false;
            n.expanded = true;
            n.name = name.empty() ? "Layer" : name;
            nodes.push_back(n);
            selection.clear();
            selection.push_back(n.id);
            anchorNodeId = n.id;
            return n.id;
        }

        int addGroupNode(const std::string& name, int parentId = 0)
        {
            LayerNode n;
            n.id = nextNodeId++;
            n.parentId = parentId;
            n.trackId = 0;
            n.isGroup = true;
            n.expanded = true;
            n.name = name.empty() ? "Group" : name;
            nodes.push_back(n);
            selection.clear();
            selection.push_back(n.id);
            anchorNodeId = n.id;
            return n.id;
        }

        int groupSelection(const std::string& name)
        {
            if (selection.empty()) return 0;

            std::vector<int> selected = selection;
            std::sort(selected.begin(), selected.end());

            int parentId = 0;
            const LayerNode* ref = findNode(selection.front());
            if (ref) parentId = ref->parentId;

            int gid = addGroupNode(name, parentId);

            for (int id : selected)
            {
                if (id == gid) continue;
                LayerNode* n = findNode(id);
                if (!n) continue;
                if (n->id == gid) continue;
                n->parentId = gid;
            }

            selection.clear();
            selection.push_back(gid);
            anchorNodeId = gid;
            return gid;
        }

        void renameNode(int nodeId, const std::string& name)
        {
            if (LayerNode* n = findNode(nodeId))
                n->name = name;
        }

        void removeNodeOnly(int nodeId)
        {
            nodes.erase(std::remove_if(nodes.begin(), nodes.end(),
                [&](const LayerNode& n) { return n.id == nodeId; }), nodes.end());
            for (auto& n : nodes)
                if (n.parentId == nodeId) n.parentId = 0;
            selection.erase(std::remove(selection.begin(), selection.end(), nodeId), selection.end());
        }

        void toggleExpanded(int nodeId)
        {
            if (LayerNode* n = findNode(nodeId))
                if (n->isGroup || hasChild(nodeId))
                    n->expanded = !n->expanded;
        }

        bool isSelected(int nodeId) const
        {
            return std::find(selection.begin(), selection.end(), nodeId) != selection.end();
        }

        std::vector<LayerRow> buildRows() const
        {
            std::vector<LayerRow> out;
            appendRowsRecursive(0, 0, out);
            return out;
        }

        bool movePrimarySelectionUp()
        {
            return moveNodeRelative(primarySelectedNodeId(), -1);
        }

        bool movePrimarySelectionDown()
        {
            return moveNodeRelative(primarySelectedNodeId(), +1);
        }

        void handleClick(int nodeId, bool ctrl, bool shift)
        {
            auto rows = buildRows();
            if (!shift)
            {
                anchorNodeId = nodeId;
            }

            if (shift && anchorNodeId != 0)
            {
                int a = -1, b = -1;
                for (int i = 0; i < (int)rows.size(); ++i)
                {
                    if (rows[(size_t)i].nodeId == anchorNodeId) a = i;
                    if (rows[(size_t)i].nodeId == nodeId) b = i;
                }
                if (a >= 0 && b >= 0)
                {
                    if (a > b) std::swap(a, b);
                    if (!ctrl) selection.clear();
                    for (int i = a; i <= b; ++i)
                    {
                        int id = rows[(size_t)i].nodeId;
                        if (!isSelected(id)) selection.push_back(id);
                    }
                    return;
                }
            }

            if (ctrl)
            {
                auto it = std::find(selection.begin(), selection.end(), nodeId);
                if (it == selection.end()) selection.push_back(nodeId);
                else selection.erase(it);
                if (selection.empty()) selection.push_back(nodeId);
                return;
            }

            selection.clear();
            selection.push_back(nodeId);
        }

        bool hasChild(int nodeId) const
        {
            for (const auto& n : nodes)
                if (n.parentId == nodeId) return true;
            return false;
        }

        bool isDescendantOf(int nodeId, int ancestorNodeId) const
        {
            if (nodeId == 0 || ancestorNodeId == 0 || nodeId == ancestorNodeId) return false;
            const LayerNode* cur = findNode(nodeId);
            while (cur && cur->parentId != 0)
            {
                if (cur->parentId == ancestorNodeId) return true;
                cur = findNode(cur->parentId);
            }
            return false;
        }

        bool saveToPath(const std::filesystem::path& filePath) const
        {
            namespace fs = std::filesystem;
            std::error_code ec;
            if (filePath.has_parent_path())
                fs::create_directories(filePath.parent_path(), ec);
            std::ofstream f(filePath, std::ios::binary);
            if (!f) return false;

            f << "{\n";
            f << "  \"nextNodeId\": " << nextNodeId << ",\n";
            f << "  \"anchorNodeId\": " << anchorNodeId << ",\n";
            f << "  \"selection\": [";
            for (size_t i = 0; i < selection.size(); ++i)
            {
                if (i) f << ",";
                f << selection[i];
            }
            f << "],\n";
            f << "  \"nodes\": [\n";
            for (size_t i = 0; i < nodes.size(); ++i)
            {
                const auto& n = nodes[i];
                f << "    {"
                  << "\"id\":" << n.id << ","
                  << "\"parentId\":" << n.parentId << ","
                  << "\"trackId\":" << n.trackId << ","
                  << "\"isGroup\":" << (n.isGroup ? "true" : "false") << ","
                  << "\"expanded\":" << (n.expanded ? "true" : "false") << ","
                  << "\"name\":\"" << escape(n.name) << "\"}";
                if (i + 1 < nodes.size()) f << ",";
                f << "\n";
            }
            f << "  ]\n}\n";
            return true;
        }

        bool saveToFolder(const std::string& folderPath) const
        {
            return saveToPath(std::filesystem::path(folderPath) / "layers.json");
        }

        bool loadFromPath(const std::filesystem::path& filePath)
        {
            clear();

            std::ifstream f(filePath, std::ios::binary);
            if (!f) return false;
            std::ostringstream ss;
            ss << f.rdbuf();
            std::string txt = ss.str();

            int nextId = findInt(txt, "nextNodeId", 1);
            nextNodeId = std::max(1, nextId);
            anchorNodeId = findInt(txt, "anchorNodeId", 0);

            std::string selArr = extractArray(txt, "selection");
            size_t selPos = 0;
            while (selPos < selArr.size())
            {
                int id = 0;
                if (parseInt(selArr, selPos, id))
                    selection.push_back(id);
                else
                    ++selPos;
            }

            std::string arr = extractArray(txt, "nodes");
            size_t i = 0;
            while (true)
            {
                size_t a = arr.find('{', i);
                if (a == std::string::npos) break;
                int depth = 0;
                size_t b = std::string::npos;
                for (size_t k = a; k < arr.size(); ++k)
                {
                    if (arr[k] == '{') depth++;
                    else if (arr[k] == '}')
                    {
                        depth--;
                        if (depth == 0) { b = k; break; }
                    }
                }
                if (b == std::string::npos) break;
                std::string obj = arr.substr(a, b - a + 1);

                LayerNode n;
                n.id = findInt(obj, "id", 0);
                n.parentId = findInt(obj, "parentId", 0);
                n.trackId = findInt(obj, "trackId", 0);
                n.isGroup = findBool(obj, "isGroup", false);
                n.expanded = findBool(obj, "expanded", true);
                n.name = findStr(obj, "name", n.isGroup ? "Group" : "Layer");
                if (n.id != 0)
                    nodes.push_back(n);

                i = b + 1;
            }

            if (!nodes.empty())
                nextNodeId = std::max(nextNodeId, maxNodeId() + 1);

            std::set<int> validIds;
            for (const auto& n : nodes) validIds.insert(n.id);
            selection.erase(
                std::remove_if(selection.begin(), selection.end(),
                    [&](int id) { return validIds.find(id) == validIds.end(); }),
                selection.end());
            if (anchorNodeId != 0 && validIds.find(anchorNodeId) == validIds.end())
                anchorNodeId = 0;

            int first = firstLayerNodeId();
            if (selection.empty() && first != 0) selection.push_back(first);
            if (anchorNodeId == 0 && !selection.empty()) anchorNodeId = selection.back();
            return !nodes.empty();
        }

        bool loadFromFolder(const std::string& folderPath)
        {
            return loadFromPath(std::filesystem::path(folderPath) / "layers.json");
        }

    private:
        void cleanupAfterTimelineSync(const std::set<int>& drawTrackIds)
        {
            nodes.erase(
                std::remove_if(nodes.begin(), nodes.end(),
                    [&](const LayerNode& n)
                    {
                        return !n.isGroup && drawTrackIds.find(n.trackId) == drawTrackIds.end();
                    }),
                nodes.end());

            std::set<int> validIds;
            for (const auto& n : nodes) validIds.insert(n.id);

            for (auto& n : nodes)
            {
                if (n.parentId != 0 && validIds.find(n.parentId) == validIds.end())
                    n.parentId = 0;
            }

            selection.erase(
                std::remove_if(selection.begin(), selection.end(),
                    [&](int id) { return validIds.find(id) == validIds.end(); }),
                selection.end());
            if (anchorNodeId != 0 && validIds.find(anchorNodeId) == validIds.end())
                anchorNodeId = 0;

            if (selection.empty())
            {
                int firstLayer = firstLayerNodeId();
                if (firstLayer != 0) selection.push_back(firstLayer);
            }
            if (anchorNodeId == 0 && !selection.empty())
                anchorNodeId = selection.back();
        }

        static bool parseInt(const std::string& s, size_t& i, int& out)
        {
            while (i < s.size() && !std::isdigit((unsigned char)s[i]) && s[i] != '-') ++i;
            if (i >= s.size()) return false;
            bool neg = false;
            if (s[i] == '-') { neg = true; ++i; }
            if (i >= s.size() || !std::isdigit((unsigned char)s[i])) return false;
            int v = 0;
            while (i < s.size() && std::isdigit((unsigned char)s[i]))
            {
                v = v * 10 + (s[i] - '0');
                ++i;
            }
            out = neg ? -v : v;
            return true;
        }

        std::vector<LayerNode> nodes;
        std::vector<int> selection;
        int nextNodeId = 1;
        int anchorNodeId = 0;

        int maxNodeId() const
        {
            int m = 0;
            for (const auto& n : nodes) m = std::max(m, n.id);
            return m;
        }

        void appendRowsRecursive(int parentId, int depth, std::vector<LayerRow>& out) const
        {
            for (const auto& n : nodes)
            {
                if (n.parentId != parentId) continue;
                out.push_back(LayerRow{ n.id, depth });
                if ((n.isGroup || hasChild(n.id)) && n.expanded)
                    appendRowsRecursive(n.id, depth + 1, out);
            }
        }

        std::vector<int> collectSubtreeIds(int rootId) const
        {
            std::vector<int> out;
            if (rootId == 0) return out;
            out.push_back(rootId);
            for (const auto& n : nodes)
            {
                if (n.parentId == rootId)
                {
                    auto child = collectSubtreeIds(n.id);
                    out.insert(out.end(), child.begin(), child.end());
                }
            }
            return out;
        }

        bool moveNodeRelative(int nodeId, int dir)
        {
            if (nodeId == 0 || dir == 0) return false;
            LayerNode* n = findNode(nodeId);
            if (!n) return false;

            std::vector<int> siblingRoots;
            for (const auto& it : nodes)
                if (it.parentId == n->parentId) siblingRoots.push_back(it.id);

            auto itPos = std::find(siblingRoots.begin(), siblingRoots.end(), nodeId);
            if (itPos == siblingRoots.end()) return false;
            int idx = (int)std::distance(siblingRoots.begin(), itPos);
            int newIdx = idx + dir;
            if (newIdx < 0 || newIdx >= (int)siblingRoots.size()) return false;

            std::vector<int> blockIds = collectSubtreeIds(nodeId);
            std::vector<LayerNode> block;
            std::vector<LayerNode> remaining;
            block.reserve(blockIds.size());
            remaining.reserve(nodes.size());
            for (const auto& it : nodes)
            {
                if (std::find(blockIds.begin(), blockIds.end(), it.id) != blockIds.end()) block.push_back(it);
                else remaining.push_back(it);
            }

            siblingRoots.erase(siblingRoots.begin() + idx);
            siblingRoots.insert(siblingRoots.begin() + newIdx, nodeId);

            std::vector<LayerNode> rebuilt;
            rebuilt.reserve(nodes.size());
            auto appendSubtree = [&](int rootId)
            {
                std::vector<int> ids = collectSubtreeIds(rootId);
                for (const auto& item : remaining)
                    if (std::find(ids.begin(), ids.end(), item.id) != ids.end()) rebuilt.push_back(item);
            };

            for (int sibRoot : siblingRoots)
            {
                if (sibRoot == nodeId)
                {
                    rebuilt.insert(rebuilt.end(), block.begin(), block.end());
                }
                else
                {
                    appendSubtree(sibRoot);
                }
            }

            for (const auto& it : remaining)
            {
                bool already = false;
                for (const auto& r : rebuilt)
                    if (r.id == it.id) { already = true; break; }
                if (!already) rebuilt.push_back(it);
            }

            if (rebuilt.size() != nodes.size()) return false;
            nodes = std::move(rebuilt);
            return true;
        }

        static std::string escape(const std::string& s)
        {
            std::string out;
            out.reserve(s.size() + 8);
            for (char c : s)
            {
                switch (c)
                {
                case '\\': out += "\\\\"; break;
                case '\"': out += "\\\""; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default: out += c; break;
                }
            }
            return out;
        }

        static int findInt(const std::string& j, const char* key, int def)
        {
            std::string k = std::string("\"") + key + "\"";
            size_t p = j.find(k);
            if (p == std::string::npos) return def;
            p = j.find(':', p);
            if (p == std::string::npos) return def;
            ++p;
            while (p < j.size() && std::isspace((unsigned char)j[p])) ++p;
            try { return std::stoi(j.substr(p)); } catch (...) { return def; }
        }

        static bool findBool(const std::string& j, const char* key, bool def)
        {
            std::string k = std::string("\"") + key + "\"";
            size_t p = j.find(k);
            if (p == std::string::npos) return def;
            p = j.find(':', p);
            if (p == std::string::npos) return def;
            ++p;
            while (p < j.size() && std::isspace((unsigned char)j[p])) ++p;
            if (j.compare(p, 4, "true") == 0) return true;
            if (j.compare(p, 5, "false") == 0) return false;
            return def;
        }

        static std::string findStr(const std::string& j, const char* key, const std::string& def)
        {
            std::string k = std::string("\"") + key + "\"";
            size_t p = j.find(k);
            if (p == std::string::npos) return def;
            p = j.find(':', p);
            if (p == std::string::npos) return def;
            p = j.find('"', p);
            if (p == std::string::npos) return def;
            ++p;
            std::string out;
            while (p < j.size())
            {
                char c = j[p++];
                if (c == '\\' && p < j.size())
                {
                    char e = j[p++];
                    switch (e)
                    {
                    case 'n': out.push_back('\n'); break;
                    case 'r': out.push_back('\r'); break;
                    case 't': out.push_back('\t'); break;
                    case '\\': out.push_back('\\'); break;
                    case '"': out.push_back('"'); break;
                    default: out.push_back(e); break;
                    }
                    continue;
                }
                if (c == '"') break;
                out.push_back(c);
            }
            return out;
        }

        static std::string extractArray(const std::string& j, const char* key)
        {
            std::string k = std::string("\"") + key + "\"";
            size_t p = j.find(k);
            if (p == std::string::npos) return {};
            p = j.find('[', p);
            if (p == std::string::npos) return {};
            int depth = 0;
            for (size_t i = p; i < j.size(); ++i)
            {
                if (j[i] == '[') depth++;
                else if (j[i] == ']')
                {
                    depth--;
                    if (depth == 0) return j.substr(p, i - p + 1);
                }
            }
            return {};
        }
    };
}
