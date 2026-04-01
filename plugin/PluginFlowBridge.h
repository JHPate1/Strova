#pragma once

#include "PluginProjectData.h"

#include <string>
#include <vector>

class App;
struct Project;
struct CaptureSample;
struct FlowLinkSample;
struct FlowLinkClip;

namespace strova::plugin
{
    struct FlowSampleView
    {
        float x = 0.0f;
        float y = 0.0f;
        float t = 0.0f;
        float pressure = 1.0f;
        int r = 255;
        int g = 255;
        int b = 255;
        int a = 255;
    };

    struct FlowSnapshotSummary
    {
        bool available = false;
        int sampleCount = 0;
        float durationSeconds = 0.0f;
        float minX = 0.0f;
        float minY = 0.0f;
        float maxX = 0.0f;
        float maxY = 0.0f;
    };

    struct FlowLinkSampleView
    {
        float posX = 0.0f;
        float posY = 0.0f;
        float rotation = 0.0f;
        float t = 0.0f;
    };

    struct FlowLinkClipSummary
    {
        int targetTrackId = 0;
        int laneIndex = 0;
        int startFrame = 0;
        int duration = 0;
        bool loop = false;
        bool relative = false;
        int sampleCount = 0;
    };

    bool captureCurrentFlowSnapshot(const App& app, FlowSnapshotSummary& outSummary, std::vector<FlowSampleView>& outSamples);
    bool captureCurrentFlowLinkSnapshot(const App& app, FlowSnapshotSummary& outSummary, std::vector<FlowLinkSampleView>& outSamples);
    bool captureFlowLinkTrackClips(const App& app, int targetTrackId, std::vector<FlowLinkClipSummary>& outClips);

    std::string buildFlowSnapshotJson(const FlowSnapshotSummary& summary, const std::vector<FlowSampleView>& samples);
    std::string buildFlowLinkSnapshotJson(const FlowSnapshotSummary& summary, const std::vector<FlowLinkSampleView>& samples);
    std::string buildFlowLinkTrackJson(int targetTrackId, const std::vector<FlowLinkClipSummary>& clips);

    bool upsertFlowProcessorAugmentation(
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
        std::string& outErr);

    bool upsertFlowLinkProcessorAugmentation(
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
        std::string& outErr);
}
