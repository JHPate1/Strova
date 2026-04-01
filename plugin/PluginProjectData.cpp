#include "PluginProjectData.h"

#include "../core/Project.h"
#include "../core/SerializationUtils.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <sstream>

namespace strova::plugin
{
    namespace
    {
        template <typename T>
        void appendIntArray(std::ostringstream& out, const std::vector<T>& values)
        {
            out << '[';
            for (std::size_t i = 0; i < values.size(); ++i)
            {
                if (i) out << ',';
                out << values[i];
            }
            out << ']';
        }

        std::string trimCopy(const std::string& s)
        {
            std::size_t begin = 0;
            std::size_t end = s.size();
            while (begin < end && std::isspace(static_cast<unsigned char>(s[begin]))) ++begin;
            while (end > begin && std::isspace(static_cast<unsigned char>(s[end - 1]))) --end;
            return s.substr(begin, end - begin);
        }

        std::string normalizeJsonValue(const std::string& text, const char* fallback)
        {
            const std::string trimmed = trimCopy(text);
            if (!trimmed.empty()) return trimmed;
            return fallback ? std::string(fallback) : std::string("null");
        }

        std::string extractValueForKey(const std::string& objectText, const char* key)
        {
            size_t pos = 0;
            if (!strova::iojson::findKeyPosAfterColon(objectText, key, pos))
                return {};
            strova::iojson::skipWs(objectText, pos);
            if (pos >= objectText.size()) return {};

            const char first = objectText[pos];
            if (first == '{')
            {
                const size_t end = strova::iojson::findMatchingBrace(objectText, pos);
                if (end == std::string::npos) return {};
                return objectText.substr(pos, end - pos + 1);
            }
            if (first == '[')
            {
                int depth = 0;
                bool inStr = false;
                for (size_t i = pos; i < objectText.size(); ++i)
                {
                    const char c = objectText[i];
                    if (c == '"' && (i == 0 || objectText[i - 1] != '\\')) inStr = !inStr;
                    if (inStr) continue;
                    if (c == '[') ++depth;
                    else if (c == ']')
                    {
                        --depth;
                        if (depth == 0) return objectText.substr(pos, i - pos + 1);
                    }
                }
                return {};
            }
            if (first == '"')
            {
                std::string parsed;
                size_t i = pos;
                if (strova::iojson::parseStringAt(objectText, i, parsed))
                    return std::string("\"") + strova::iojson::jsonEscape(parsed) + "\"";
                return {};
            }

            size_t end = pos;
            while (end < objectText.size())
            {
                const char c = objectText[end];
                if (c == ',' || c == '}' || c == '\n' || c == '\r')
                    break;
                ++end;
            }
            return trimCopy(objectText.substr(pos, end - pos));
        }

        void parseIntArray(const std::string& arrayText, std::vector<int>& outValues)
        {
            outValues.clear();
            size_t pos = arrayText.find('[');
            if (pos == std::string::npos) return;
            ++pos;
            while (pos < arrayText.size())
            {
                strova::iojson::skipWs(arrayText, pos);
                if (pos >= arrayText.size() || arrayText[pos] == ']') break;
                int value = 0;
                if (!strova::iojson::parseIntAt(arrayText, pos, value)) break;
                outValues.push_back(value);
                strova::iojson::skipWs(arrayText, pos);
                if (pos < arrayText.size() && arrayText[pos] == ',') ++pos;
            }
        }

        void parseDependencyObject(const std::string& obj, PluginDependency& dep)
        {
            dep.pluginId = strova::iojson::findStr(obj, "plugin_id", "");
            dep.displayName = strova::iojson::findStr(obj, "display_name", "");
            dep.savedWithPluginVersion = strova::iojson::findStr(obj, "saved_with_plugin_version", "");
            dep.contentInstances = strova::iojson::findInt(obj, "content_instances", 0);
            dep.fallbackAvailable = false;
            size_t pos = 0;
            if (strova::iojson::findKeyPosAfterColon(obj, "fallback_available", pos))
                strova::iojson::parseBoolAt(obj, pos, dep.fallbackAvailable);
            dep.missingBehavior = strova::iojson::findStr(obj, "missing_behavior", "preserve_unresolved");
            parseIntArray(extractValueForKey(obj, "required_schema_versions"), dep.requiredSchemaVersions);
        }

        void parseAttachmentObject(const std::string& obj, PluginContentAttachment& at)
        {
            at.kind = strova::iojson::findStr(obj, "kind", "");
            at.trackId = strova::iojson::findStr(obj, "track_id", "");
            at.startFrame = strova::iojson::findInt(obj, "start_frame", 0);
            at.endFrame = strova::iojson::findInt(obj, "end_frame", 0);
            at.frame = strova::iojson::findInt(obj, "frame", 0);
            at.ownerId = strova::iojson::findStr(obj, "owner_id", "");
        }

        void parseContentObject(const std::string& obj, PluginContentRecord& rec)
        {
            rec.pluginId = strova::iojson::findStr(obj, "plugin_id", "");
            rec.contentTypeId = strova::iojson::findStr(obj, "content_type_id", "");
            rec.pluginVersionSaved = strova::iojson::findStr(obj, "plugin_version_saved", "");
            rec.contentSchemaVersion = strova::iojson::findInt(obj, "content_schema_version", 1);
            rec.instanceId = strova::iojson::findStr(obj, "instance_id", "");
            rec.payloadJson = normalizeJsonValue(extractValueForKey(obj, "payload"), "{}");
            rec.fallbackProxyJson = normalizeJsonValue(extractValueForKey(obj, "fallback_proxy"), "null");
            rec.unresolved = false;
            size_t pos = 0;
            if (strova::iojson::findKeyPosAfterColon(obj, "unresolved", pos))
                strova::iojson::parseBoolAt(obj, pos, rec.unresolved);
            rec.unresolvedReason = strova::iojson::findStr(obj, "unresolved_reason", "");
            const std::string attachText = extractValueForKey(obj, "attachment");
            if (!attachText.empty()) parseAttachmentObject(attachText, rec.attachment);
        }

        bool parseProjectStateEntries(const std::string& objectText, std::vector<PluginProjectStateEntry>& outEntries)
        {
            outEntries.clear();
            std::size_t i = 0;
            while (i < objectText.size() && objectText[i] != '{') ++i;
            if (i >= objectText.size()) return false;
            ++i;
            while (i < objectText.size())
            {
                strova::iojson::skipWs(objectText, i);
                if (i >= objectText.size() || objectText[i] == '}') break;

                std::string key;
                if (!strova::iojson::parseStringAt(objectText, i, key)) break;
                strova::iojson::skipWs(objectText, i);
                if (i >= objectText.size() || objectText[i] != ':') break;
                ++i;
                strova::iojson::skipWs(objectText, i);
                if (i >= objectText.size()) break;

                std::string value;
                if (objectText[i] == '{')
                {
                    const std::size_t end = strova::iojson::findMatchingBrace(objectText, i);
                    if (end == std::string::npos) break;
                    value = objectText.substr(i, end - i + 1);
                    i = end + 1;
                }
                else if (objectText[i] == '[')
                {
                    int depth = 0;
                    bool inStr = false;
                    std::size_t end = i;
                    for (; end < objectText.size(); ++end)
                    {
                        const char c = objectText[end];
                        if (c == '"' && (end == 0 || objectText[end - 1] != '\\')) inStr = !inStr;
                        if (inStr) continue;
                        if (c == '[') ++depth;
                        else if (c == ']')
                        {
                            --depth;
                            if (depth == 0) break;
                        }
                    }
                    if (end >= objectText.size()) break;
                    value = objectText.substr(i, end - i + 1);
                    i = end + 1;
                }
                else if (objectText[i] == '"')
                {
                    std::string parsed;
                    std::size_t j = i;
                    if (!strova::iojson::parseStringAt(objectText, j, parsed)) break;
                    value = std::string("\"") + strova::iojson::jsonEscape(parsed) + "\"";
                    i = j;
                }
                else
                {
                    std::size_t end = i;
                    while (end < objectText.size() && objectText[end] != ',' && objectText[end] != '}') ++end;
                    value = trimCopy(objectText.substr(i, end - i));
                    i = end;
                }

                outEntries.push_back({ key, normalizeJsonValue(value, "{}") });
                strova::iojson::skipWs(objectText, i);
                if (i < objectText.size() && objectText[i] == ',') ++i;
            }
            return true;
        }
    }

    void collectPluginDependencyUsage(Project& project)
    {
        std::map<std::string, PluginDependency> merged;
        for (const PluginDependency& dep : project.pluginDependencies)
        {
            if (dep.pluginId.empty()) continue;
            merged[dep.pluginId] = dep;
            merged[dep.pluginId].contentInstances = 0;
            merged[dep.pluginId].fallbackAvailable = dep.fallbackAvailable;
        }

        for (const PluginContentRecord& content : project.pluginContents)
        {
            if (content.pluginId.empty()) continue;
            PluginDependency& dep = merged[content.pluginId];
            dep.pluginId = content.pluginId;
            if (dep.displayName.empty()) dep.displayName = content.pluginId;
            if (dep.savedWithPluginVersion.empty()) dep.savedWithPluginVersion = content.pluginVersionSaved;
            dep.contentInstances += 1;
            if (!content.fallbackProxyJson.empty() && content.fallbackProxyJson != "null" && content.fallbackProxyJson != "{}")
                dep.fallbackAvailable = true;
            if (std::find(dep.requiredSchemaVersions.begin(), dep.requiredSchemaVersions.end(), content.contentSchemaVersion) == dep.requiredSchemaVersions.end())
                dep.requiredSchemaVersions.push_back(content.contentSchemaVersion);
        }

        project.pluginDependencies.clear();
        for (auto& it : merged)
        {
            auto& dep = it.second;
            std::sort(dep.requiredSchemaVersions.begin(), dep.requiredSchemaVersions.end());
            dep.requiredSchemaVersions.erase(std::unique(dep.requiredSchemaVersions.begin(), dep.requiredSchemaVersions.end()), dep.requiredSchemaVersions.end());
            project.pluginDependencies.push_back(std::move(dep));
        }
    }

    bool serializeProjectPluginData(const Project& inputProject, std::string& outJsonAppend)
    {
        Project project = inputProject;
        collectPluginDependencyUsage(project);

        std::ostringstream out;
        out << ",\n  \"plugin_dependencies\": [\n";
        for (std::size_t i = 0; i < project.pluginDependencies.size(); ++i)
        {
            const PluginDependency& dep = project.pluginDependencies[i];
            out << "    {"
                << "\"plugin_id\":\"" << strova::iojson::jsonEscape(dep.pluginId) << "\"," 
                << "\"display_name\":\"" << strova::iojson::jsonEscape(dep.displayName) << "\"," 
                << "\"saved_with_plugin_version\":\"" << strova::iojson::jsonEscape(dep.savedWithPluginVersion) << "\"," 
                << "\"required_schema_versions\":";
            appendIntArray(out, dep.requiredSchemaVersions);
            out << ",\"content_instances\":" << dep.contentInstances
                << ",\"missing_behavior\":\"" << strova::iojson::jsonEscape(dep.missingBehavior) << "\"," 
                << "\"fallback_available\":" << (dep.fallbackAvailable ? "true" : "false")
                << "}";
            if (i + 1 < project.pluginDependencies.size()) out << ',';
            out << '\n';
        }
        out << "  ],\n";

        out << "  \"plugin_project_state\": {\n";
        for (std::size_t i = 0; i < project.pluginProjectStates.size(); ++i)
        {
            const PluginProjectStateEntry& st = project.pluginProjectStates[i];
            out << "    \"" << strova::iojson::jsonEscape(st.pluginId) << "\": " << normalizeJsonValue(st.stateJson, "{}");
            if (i + 1 < project.pluginProjectStates.size()) out << ',';
            out << '\n';
        }
        out << "  },\n";

        out << "  \"plugin_content\": [\n";
        for (std::size_t i = 0; i < project.pluginContents.size(); ++i)
        {
            const PluginContentRecord& rec = project.pluginContents[i];
            out << "    {"
                << "\"plugin_id\":\"" << strova::iojson::jsonEscape(rec.pluginId) << "\"," 
                << "\"content_type_id\":\"" << strova::iojson::jsonEscape(rec.contentTypeId) << "\"," 
                << "\"plugin_version_saved\":\"" << strova::iojson::jsonEscape(rec.pluginVersionSaved) << "\"," 
                << "\"content_schema_version\":" << rec.contentSchemaVersion << ','
                << "\"instance_id\":\"" << strova::iojson::jsonEscape(rec.instanceId) << "\"," 
                << "\"attachment\":{"
                << "\"kind\":\"" << strova::iojson::jsonEscape(rec.attachment.kind) << "\"," 
                << "\"track_id\":\"" << strova::iojson::jsonEscape(rec.attachment.trackId) << "\"," 
                << "\"start_frame\":" << rec.attachment.startFrame << ','
                << "\"end_frame\":" << rec.attachment.endFrame << ','
                << "\"frame\":" << rec.attachment.frame << ','
                << "\"owner_id\":\"" << strova::iojson::jsonEscape(rec.attachment.ownerId) << "\"},"
                << "\"payload\":" << normalizeJsonValue(rec.payloadJson, "{}") << ','
                << "\"fallback_proxy\":" << normalizeJsonValue(rec.fallbackProxyJson, "null") << ','
                << "\"unresolved\":" << (rec.unresolved ? "true" : "false") << ','
                << "\"unresolved_reason\":\"" << strova::iojson::jsonEscape(rec.unresolvedReason) << "\""
                << "}";
            if (i + 1 < project.pluginContents.size()) out << ',';
            out << '\n';
        }
        out << "  ]";

        outJsonAppend = out.str();
        return true;
    }

    bool loadProjectPluginDataFromJson(const std::string& projectJson, Project& project, std::string& outErr)
    {
        outErr.clear();
        project.pluginDependencies.clear();
        project.pluginContents.clear();
        project.pluginProjectStates.clear();

        const std::string depArray = strova::iojson::extractArrayText(projectJson, "plugin_dependencies");
        if (!depArray.empty())
        {
            std::vector<std::string> objs;
            strova::iojson::parseObjectsInArray(depArray, objs);
            for (const std::string& obj : objs)
            {
                PluginDependency dep{};
                parseDependencyObject(obj, dep);
                if (!dep.pluginId.empty()) project.pluginDependencies.push_back(std::move(dep));
            }
        }

        const std::string contentArray = strova::iojson::extractArrayText(projectJson, "plugin_content");
        if (!contentArray.empty())
        {
            std::vector<std::string> objs;
            strova::iojson::parseObjectsInArray(contentArray, objs);
            for (const std::string& obj : objs)
            {
                PluginContentRecord rec{};
                parseContentObject(obj, rec);
                if (!rec.pluginId.empty()) project.pluginContents.push_back(std::move(rec));
            }
        }

        const std::string stateObject = extractValueForKey(projectJson, "plugin_project_state");
        if (!stateObject.empty())
            parseProjectStateEntries(stateObject, project.pluginProjectStates);

        collectPluginDependencyUsage(project);
        return true;
    }
}
