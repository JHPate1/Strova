#include "PluginFlowBridge.h"

#include "PluginDocumentModel.h"
#include "PluginRegistry.h"
#include "../app/App.h"
#include "../core/FlowCapturer.h"
#include "../core/DrawingEngine.h"
#include "../core/Project.h"
#include "../core/SerializationUtils.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace strova::plugin
{
    namespace
    {
        template <typename T>
        std::string jsonNumber(T value)
        {
            std::ostringstream out;
            out << value;
            return out.str();
        }

        std::string escapeJson(const std::string& value)
        {
            return strova::iojson::jsonEscape(value);
        }

        std::string makeProcessorInstanceId(const std::string& ownerPluginId, const std::string& processorId, const PluginContentAttachment& attachment)
        {
            std::string safeProcessor = processorId.empty() ? "processor" : processorId;
            for (char& c : safeProcessor)
            {
                if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '.'))
                    c = '_';
            }
            return ownerPluginId + ":" + safeProcessor + ":" + makeAttachmentKey(attachment);
        }

        void fillBounds(FlowSnapshotSummary& summary, float x, float y)
        {
            if (summary.sampleCount == 0)
            {
                summary.minX = summary.maxX = x;
                summary.minY = summary.maxY = y;
                return;
            }
            summary.minX = std::min(summary.minX, x);
            summary.minY = std::min(summary.minY, y);
            summary.maxX = std::max(summary.maxX, x);
            summary.maxY = std::max(summary.maxY, y);
        }

        bool upsertProcessorAugmentation(
            Project& project,
            const std::string& ownerPluginId,
            const std::string& contentTypeId,
            int schemaVersion,
            const std::string& pluginVersion,
            const std::string& processorId,
            const PluginContentAttachment& attachment,
            const std::string& payloadJson,
            const std::string& fallbackProxyJson,
            std::string& outInstanceId,
            std::string& outErr)
        {
            outErr.clear();
            if (ownerPluginId.empty())
            {
                outErr = "Owner plugin id is empty.";
                return false;
            }
            if (contentTypeId.empty())
            {
                outErr = "Content type id is empty.";
                return false;
            }
            if (processorId.empty())
            {
                outErr = "Processor id is empty.";
                return false;
            }

            outInstanceId = makeProcessorInstanceId(ownerPluginId, processorId, attachment);
            PluginContentRecord record{};
            record.pluginId = ownerPluginId;
            record.contentTypeId = contentTypeId;
            record.pluginVersionSaved = pluginVersion;
            record.contentSchemaVersion = schemaVersion <= 0 ? 1 : schemaVersion;
            record.instanceId = outInstanceId;
            record.attachment = attachment;
            record.payloadJson = payloadJson.empty() ? "{}" : payloadJson;
            record.fallbackProxyJson = fallbackProxyJson.empty() ? "null" : fallbackProxyJson;
            record.unresolved = false;
            record.unresolvedReason.clear();

            if (!upsertRuntimeObject(project.pluginRuntime, record, nullptr, outErr))
                return false;
            syncRuntimeStoreToProject(project.pluginRuntime, project.pluginContents);
            collectPluginDependencyUsage(project);
            return true;
        }
    }

    bool captureCurrentFlowSnapshot(const App& app, FlowSnapshotSummary& outSummary, std::vector<FlowSampleView>& outSamples)
    {
        outSummary = {};
        outSamples.clear();
        const auto& samples = app.flowCapturer().getSamples();
        if (samples.empty()) return false;

        outSummary.available = true;
        outSummary.sampleCount = static_cast<int>(samples.size());
        outSummary.durationSeconds = samples.back().t;
        outSamples.reserve(samples.size());
        for (const CaptureSample& sample : samples)
        {
            FlowSampleView view{};
            view.x = sample.x;
            view.y = sample.y;
            view.t = sample.t;
            view.pressure = sample.pressure;
            view.r = sample.color.r;
            view.g = sample.color.g;
            view.b = sample.color.b;
            view.a = sample.color.a;
            fillBounds(outSummary, sample.x, sample.y);
            outSamples.push_back(view);
        }
        return true;
    }

    bool captureCurrentFlowLinkSnapshot(const App& app, FlowSnapshotSummary& outSummary, std::vector<FlowLinkSampleView>& outSamples)
    {
        outSummary = {};
        outSamples.clear();
        const auto& samples = app.flowCapturer().getFlowLinkSamples();
        if (samples.empty()) return false;

        outSummary.available = true;
        outSummary.sampleCount = static_cast<int>(samples.size());
        outSummary.durationSeconds = samples.back().t;
        outSamples.reserve(samples.size());
        for (const FlowLinkSample& sample : samples)
        {
            FlowLinkSampleView view{};
            view.posX = sample.posX;
            view.posY = sample.posY;
            view.rotation = sample.rotation;
            view.t = sample.t;
            fillBounds(outSummary, sample.posX, sample.posY);
            outSamples.push_back(view);
        }
        return true;
    }

    bool captureFlowLinkTrackClips(const App& app, int targetTrackId, std::vector<FlowLinkClipSummary>& outClips)
    {
        outClips.clear();
        if (targetTrackId <= 0) return false;
        const auto& clips = app.getEngine().getFlowLinkClips(targetTrackId);
        if (clips.empty()) return false;
        outClips.reserve(clips.size());
        for (const FlowLinkClip& clip : clips)
        {
            FlowLinkClipSummary summary{};
            summary.targetTrackId = clip.targetTrackId;
            summary.laneIndex = clip.laneIndex;
            summary.startFrame = clip.startFrame;
            summary.duration = clip.duration;
            summary.loop = clip.loop;
            summary.relative = clip.relative;
            summary.sampleCount = static_cast<int>(clip.samples.size());
            outClips.push_back(summary);
        }
        return true;
    }

    std::string buildFlowSnapshotJson(const FlowSnapshotSummary& summary, const std::vector<FlowSampleView>& samples)
    {
        std::ostringstream out;
        out << "{\"available\":" << (summary.available ? "true" : "false")
            << ",\"sample_count\":" << summary.sampleCount
            << ",\"duration_seconds\":" << summary.durationSeconds
            << ",\"bounds\":{\"min_x\":" << summary.minX
            << ",\"min_y\":" << summary.minY
            << ",\"max_x\":" << summary.maxX
            << ",\"max_y\":" << summary.maxY << "}"
            << ",\"samples\":[";
        for (std::size_t i = 0; i < samples.size(); ++i)
        {
            const FlowSampleView& s = samples[i];
            out << "{\"x\":" << s.x << ",\"y\":" << s.y << ",\"t\":" << s.t
                << ",\"pressure\":" << s.pressure
                << ",\"color\":{\"r\":" << s.r << ",\"g\":" << s.g << ",\"b\":" << s.b << ",\"a\":" << s.a << "}}";
            if (i + 1 < samples.size()) out << ',';
        }
        out << "]}";
        return out.str();
    }

    std::string buildFlowLinkSnapshotJson(const FlowSnapshotSummary& summary, const std::vector<FlowLinkSampleView>& samples)
    {
        std::ostringstream out;
        out << "{\"available\":" << (summary.available ? "true" : "false")
            << ",\"sample_count\":" << summary.sampleCount
            << ",\"duration_seconds\":" << summary.durationSeconds
            << ",\"bounds\":{\"min_x\":" << summary.minX
            << ",\"min_y\":" << summary.minY
            << ",\"max_x\":" << summary.maxX
            << ",\"max_y\":" << summary.maxY << "}"
            << ",\"samples\":[";
        for (std::size_t i = 0; i < samples.size(); ++i)
        {
            const FlowLinkSampleView& s = samples[i];
            out << "{\"pos_x\":" << s.posX << ",\"pos_y\":" << s.posY << ",\"rotation\":" << s.rotation << ",\"t\":" << s.t << "}";
            if (i + 1 < samples.size()) out << ',';
        }
        out << "]}";
        return out.str();
    }

    std::string buildFlowLinkTrackJson(int targetTrackId, const std::vector<FlowLinkClipSummary>& clips)
    {
        std::ostringstream out;
        out << "{\"target_track_id\":" << targetTrackId << ",\"clips\":[";
        for (std::size_t i = 0; i < clips.size(); ++i)
        {
            const FlowLinkClipSummary& c = clips[i];
            out << "{\"target_track_id\":" << c.targetTrackId
                << ",\"lane_index\":" << c.laneIndex
                << ",\"start_frame\":" << c.startFrame
                << ",\"duration\":" << c.duration
                << ",\"loop\":" << (c.loop ? "true" : "false")
                << ",\"relative\":" << (c.relative ? "true" : "false")
                << ",\"sample_count\":" << c.sampleCount << "}";
            if (i + 1 < clips.size()) out << ',';
        }
        out << "]}";
        return out.str();
    }

    bool upsertFlowProcessorAugmentation(Project& project, const std::string& ownerPluginId, const std::string& contentTypeId, int schemaVersion, const std::string& pluginVersion, const std::string& processorId, const PluginContentAttachment& attachment, const std::string& payloadJson, const std::string& fallbackProxyJson, std::string& outInstanceId, std::string& outErr)
    {
        return upsertProcessorAugmentation(project, ownerPluginId, contentTypeId, schemaVersion, pluginVersion, processorId, attachment, payloadJson, fallbackProxyJson, outInstanceId, outErr);
    }

    bool upsertFlowLinkProcessorAugmentation(Project& project, const std::string& ownerPluginId, const std::string& contentTypeId, int schemaVersion, const std::string& pluginVersion, const std::string& processorId, const PluginContentAttachment& attachment, const std::string& payloadJson, const std::string& fallbackProxyJson, std::string& outInstanceId, std::string& outErr)
    {
        return upsertProcessorAugmentation(project, ownerPluginId, contentTypeId, schemaVersion, pluginVersion, processorId, attachment, payloadJson, fallbackProxyJson, outInstanceId, outErr);
    }
}
