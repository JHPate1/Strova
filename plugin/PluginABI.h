#pragma once

#include <stdint.h>

#ifdef _WIN32
#  ifdef STROVA_PLUGIN_BUILD
#    define STROVA_PLUGIN_EXPORT __declspec(dllexport)
#  else
#    define STROVA_PLUGIN_EXPORT __declspec(dllimport)
#  endif
#else
#  define STROVA_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

#define STROVA_PLUGIN_ABI_VERSION 1u
#define STROVA_PLUGIN_HOST_API_VERSION 4u

#ifdef __cplusplus
extern "C" {
#endif

enum StrovaPluginCapabilityMask
{
    STROVA_PLUGIN_CAP_COMMANDS = 1ull << 0,
    STROVA_PLUGIN_CAP_DOCK_PANELS = 1ull << 1,
    STROVA_PLUGIN_CAP_IMPORTERS = 1ull << 2,
    STROVA_PLUGIN_CAP_EXPORTERS = 1ull << 3,
    STROVA_PLUGIN_CAP_PROJECT_CONTENT = 1ull << 4,
    STROVA_PLUGIN_CAP_FLOW_PROCESSORS = 1ull << 5,
    STROVA_PLUGIN_CAP_FLOWLINK_PROCESSORS = 1ull << 6,
    STROVA_PLUGIN_CAP_CANVAS_OVERLAYS = 1ull << 7,
    STROVA_PLUGIN_CAP_BRUSH_EFFECTS = 1ull << 8,
    STROVA_PLUGIN_CAP_STROKE_EFFECTS = 1ull << 9,
    STROVA_PLUGIN_CAP_ANALYSIS_TOOLS = 1ull << 10,
    STROVA_PLUGIN_CAP_LAYER_TOOLS = 1ull << 11,
    STROVA_PLUGIN_CAP_EXPORT_PASSES = 1ull << 12,
    STROVA_PLUGIN_CAP_DOCUMENT_VALIDATORS = 1ull << 13
};

enum StrovaPluginPermissionMask
{
    STROVA_PLUGIN_PERM_PROJECT_READ = 1ull << 0,
    STROVA_PLUGIN_PERM_PROJECT_WRITE = 1ull << 1,
    STROVA_PLUGIN_PERM_PLUGIN_STORAGE = 1ull << 2,
    STROVA_PLUGIN_PERM_BULK_DELETE_PROJECT_FILES = 1ull << 3,
    STROVA_PLUGIN_PERM_OUTSIDE_PROJECT_FS = 1ull << 4
};

typedef struct StrovaHostApiHeader
{
    uint32_t struct_size;
    uint32_t host_api_version;
    uint32_t abi_version;
    const char* app_version;
    const char* platform;
    void (*log_fn)(const char* scope, const char* message);
} StrovaHostApiHeader;

typedef struct StrovaPluginCommandDesc
{
    uint32_t struct_size;
    const char* id;
    const char* title;
    const char* category;
    const char* icon_path;
} StrovaPluginCommandDesc;

typedef struct StrovaPluginDockPanelDesc
{
    uint32_t struct_size;
    const char* id;
    const char* title;
    const char* default_dock_zone;
    int min_width;
    int min_height;
    int preferred_width;
    int preferred_height;
} StrovaPluginDockPanelDesc;

typedef struct StrovaPluginImportExportDesc
{
    uint32_t struct_size;
    const char* id;
    const char* display_name;
    const char* extensions_csv;
} StrovaPluginImportExportDesc;

typedef struct StrovaPluginContentTypeDesc
{
    uint32_t struct_size;
    const char* id;
    const char* display_name;
    int schema_version;
} StrovaPluginContentTypeDesc;

typedef struct StrovaPluginProcessorDesc
{
    uint32_t struct_size;
    const char* id;
    const char* title;
    const char* input_kind;
    const char* output_type_id;
    int schema_version;
} StrovaPluginProcessorDesc;

typedef struct StrovaPluginContentAttachmentDesc
{
    const char* kind;
    const char* track_id;
    int start_frame;
    int end_frame;
    int frame;
    const char* owner_id;
} StrovaPluginContentAttachmentDesc;

typedef struct StrovaPluginContentMutationDesc
{
    uint32_t struct_size;
    const char* instance_id;
    const char* content_type_id;
    int content_schema_version;
    const char* plugin_version_saved;
    StrovaPluginContentAttachmentDesc attachment;
    const char* payload_json;
    const char* fallback_proxy_json;
    int unresolved;
    const char* unresolved_reason;
} StrovaPluginContentMutationDesc;

typedef struct StrovaPluginFlowBridgeRequest
{
    uint32_t struct_size;
    const char* processor_id;
    const char* output_type_id;
    int content_schema_version;
    const char* plugin_version_saved;
    StrovaPluginContentAttachmentDesc attachment;
    const char* payload_json;
    const char* fallback_proxy_json;
} StrovaPluginFlowBridgeRequest;

typedef struct StrovaPluginJsonBuffer
{
    uint32_t struct_size;
    char* data;
    uint32_t capacity;
    uint32_t written;
} StrovaPluginJsonBuffer;

typedef struct StrovaPluginTextBuffer
{
    uint32_t struct_size;
    char* data;
    uint32_t capacity;
    uint32_t written;
} StrovaPluginTextBuffer;

typedef struct StrovaPluginCommandContext
{
    uint32_t struct_size;
    const char* plugin_id;
    const char* command_id;
} StrovaPluginCommandContext;

typedef struct StrovaPluginDockPanelRenderContext
{
    uint32_t struct_size;
    const char* plugin_id;
    const char* panel_id;
    const char* panel_title;
    int panel_x;
    int panel_y;
    int panel_w;
    int panel_h;
    int mouse_x;
    int mouse_y;
    int hovered;
    int focused;
} StrovaPluginDockPanelRenderContext;

typedef struct StrovaPluginDockPanelEventContext
{
    uint32_t struct_size;
    const char* plugin_id;
    const char* panel_id;
    uint32_t event_type;
    int mouse_x;
    int mouse_y;
    int button;
    int wheel_x;
    int wheel_y;
    int keycode;
    int modifiers;
    char text_utf8[64];
} StrovaPluginDockPanelEventContext;

typedef struct StrovaPluginFlowProcessContext
{
    uint32_t struct_size;
    const char* plugin_id;
    const char* processor_id;
    int project_fps;
    int active_frame_index;
    const char* input_kind;
    const char* output_type_id;
    const char* snapshot_json;
} StrovaPluginFlowProcessContext;

typedef struct StrovaPluginFlowLinkProcessContext
{
    uint32_t struct_size;
    const char* plugin_id;
    const char* processor_id;
    int project_fps;
    int active_frame_index;
    int target_track_id;
    const char* input_kind;
    const char* output_type_id;
    const char* snapshot_json;
    const char* track_json;
} StrovaPluginFlowLinkProcessContext;

typedef struct StrovaPluginCanvasOverlayContext
{
    uint32_t struct_size;
    const char* plugin_id;
    int canvas_x;
    int canvas_y;
    int canvas_w;
    int canvas_h;
    float zoom;
    float pan_x;
    float pan_y;
    int mouse_x;
    int mouse_y;
    int active_frame_index;
    int active_track_id;
    const char* layer_tree_json;
    const char* selection_json;
} StrovaPluginCanvasOverlayContext;

typedef struct StrovaPluginAnalysisContext
{
    uint32_t struct_size;
    const char* plugin_id;
    int active_frame_index;
    int active_track_id;
    const char* layer_tree_json;
    const char* selection_json;
    const char* snapshot_json;
} StrovaPluginAnalysisContext;

typedef struct StrovaPluginBrushEffectContext
{
    uint32_t struct_size;
    const char* plugin_id;
    int active_frame_index;
    int active_track_id;
    int canvas_x;
    int canvas_y;
    int canvas_w;
    int canvas_h;
    float zoom;
    const char* layer_tree_json;
    const char* selection_json;
} StrovaPluginBrushEffectContext;

typedef struct StrovaPluginStrokeEffectContext
{
    uint32_t struct_size;
    const char* plugin_id;
    int active_frame_index;
    int active_track_id;
    int canvas_x;
    int canvas_y;
    int canvas_w;
    int canvas_h;
    float zoom;
    const char* layer_tree_json;
    const char* selection_json;
} StrovaPluginStrokeEffectContext;

typedef struct StrovaPluginFileRunContext
{
    uint32_t struct_size;
    const char* plugin_id;
    const char* operation_id;
    const char* source_path;
    const char* destination_path;
} StrovaPluginFileRunContext;

typedef struct StrovaPluginExportPassContext
{
    uint32_t struct_size;
    const char* plugin_id;
    const char* pass_id;
    int active_frame_index;
    int canvas_w;
    int canvas_h;
    const char* layer_tree_json;
    const char* selection_json;
    const char* export_context_json;
} StrovaPluginExportPassContext;

typedef struct StrovaPluginDocumentValidatorContext
{
    uint32_t struct_size;
    const char* plugin_id;
    int active_frame_index;
    int active_track_id;
    const char* layer_tree_json;
    const char* selection_json;
    const char* snapshot_json;
} StrovaPluginDocumentValidatorContext;

enum StrovaPluginDockPanelEventType
{
    STROVA_PLUGIN_PANEL_EVENT_NONE = 0,
    STROVA_PLUGIN_PANEL_EVENT_MOUSE_DOWN = 1,
    STROVA_PLUGIN_PANEL_EVENT_MOUSE_UP = 2,
    STROVA_PLUGIN_PANEL_EVENT_MOUSE_MOVE = 3,
    STROVA_PLUGIN_PANEL_EVENT_MOUSE_WHEEL = 4,
    STROVA_PLUGIN_PANEL_EVENT_KEY_DOWN = 5,
    STROVA_PLUGIN_PANEL_EVENT_KEY_UP = 6,
    STROVA_PLUGIN_PANEL_EVENT_TEXT_INPUT = 7
};

typedef struct StrovaPluginHostServices
{
    uint32_t struct_size;
    uint32_t host_api_version;
    int (*register_command_fn)(void* registration_scope, const StrovaPluginCommandDesc* desc);
    int (*register_dock_panel_fn)(void* registration_scope, const StrovaPluginDockPanelDesc* desc);
    int (*register_importer_fn)(void* registration_scope, const StrovaPluginImportExportDesc* desc);
    int (*register_exporter_fn)(void* registration_scope, const StrovaPluginImportExportDesc* desc);
    int (*register_content_type_fn)(void* registration_scope, const StrovaPluginContentTypeDesc* desc);
    int (*register_flow_processor_fn)(void* registration_scope, const StrovaPluginProcessorDesc* desc);
    int (*register_flowlink_processor_fn)(void* registration_scope, const StrovaPluginProcessorDesc* desc);
    int (*set_warning_fn)(void* registration_scope, const char* warning_message);
    int (*upsert_project_content_fn)(void* registration_scope, const StrovaPluginContentMutationDesc* desc);
    int (*remove_project_content_fn)(void* registration_scope, const char* instance_id);
    int (*set_project_state_fn)(void* registration_scope, const char* state_json);
    int (*copy_flow_snapshot_json_fn)(void* registration_scope, StrovaPluginJsonBuffer* out_json);
    int (*copy_flowlink_snapshot_json_fn)(void* registration_scope, StrovaPluginJsonBuffer* out_json);
    int (*copy_flowlink_track_json_fn)(void* registration_scope, int target_track_id, StrovaPluginJsonBuffer* out_json);
    int (*copy_layer_tree_json_fn)(void* registration_scope, StrovaPluginJsonBuffer* out_json);
    int (*copy_active_selection_json_fn)(void* registration_scope, StrovaPluginJsonBuffer* out_json);
    int (*upsert_flow_augmentation_fn)(void* registration_scope, const StrovaPluginFlowBridgeRequest* request, StrovaPluginJsonBuffer* out_instance_id_json);
    int (*upsert_flowlink_augmentation_fn)(void* registration_scope, const StrovaPluginFlowBridgeRequest* request, StrovaPluginJsonBuffer* out_instance_id_json);
    int (*queue_notification_fn)(void* registration_scope, const char* title, const char* message);
    int (*request_panel_open_fn)(void* registration_scope, const char* panel_id);
    int (*request_panel_focus_fn)(void* registration_scope, const char* panel_id);
    int (*request_redraw_fn)(void* registration_scope);
    int (*open_file_dialog_fn)(void* registration_scope, StrovaPluginTextBuffer* out_path);
    int (*save_file_dialog_fn)(void* registration_scope, const char* suggested_name, StrovaPluginTextBuffer* out_path);
} StrovaPluginHostServices;

typedef struct StrovaPluginQuery
{
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t plugin_api_version;
    const char* plugin_id;
    const char* plugin_version;
    const char* display_name;
    uint64_t capability_mask;
    uint64_t permission_mask;
} StrovaPluginQuery;

typedef struct StrovaPluginCreateInfo
{
    uint32_t struct_size;
    uint32_t host_api_version;
    const StrovaHostApiHeader* host;
    const StrovaPluginHostServices* services;
    void* registration_scope;
    const char* plugin_id;
    const char* plugin_root;
    const char* plugin_storage_root;
} StrovaPluginCreateInfo;

typedef struct StrovaPluginInstance
{
    void* opaque;
} StrovaPluginInstance;

typedef int (*StrovaPlugin_QueryFn)(const StrovaHostApiHeader* host, StrovaPluginQuery* out_query);
typedef int (*StrovaPlugin_CreateFn)(const StrovaPluginCreateInfo* create_info, StrovaPluginInstance** out_instance);
typedef void (*StrovaPlugin_DestroyFn)(StrovaPluginInstance* instance);
typedef int (*StrovaPlugin_InvokeCommandFn)(StrovaPluginInstance* instance, const StrovaPluginCommandContext* context);
typedef int (*StrovaPlugin_RenderDockPanelFn)(StrovaPluginInstance* instance, const StrovaPluginDockPanelRenderContext* context, StrovaPluginTextBuffer* out_text);
typedef int (*StrovaPlugin_HandleDockPanelEventFn)(StrovaPluginInstance* instance, const StrovaPluginDockPanelEventContext* context);
typedef int (*StrovaPlugin_RunImporterFn)(StrovaPluginInstance* instance, const StrovaPluginFileRunContext* context);
typedef int (*StrovaPlugin_RunExporterFn)(StrovaPluginInstance* instance, const StrovaPluginFileRunContext* context);
typedef int (*StrovaPlugin_ProcessFlowFn)(StrovaPluginInstance* instance, const StrovaPluginFlowProcessContext* context, StrovaPluginTextBuffer* out_result_json);
typedef int (*StrovaPlugin_ProcessFlowLinkFn)(StrovaPluginInstance* instance, const StrovaPluginFlowLinkProcessContext* context, StrovaPluginTextBuffer* out_result_json);
typedef int (*StrovaPlugin_RenderCanvasOverlayFn)(StrovaPluginInstance* instance, const StrovaPluginCanvasOverlayContext* context, StrovaPluginTextBuffer* out_draw_commands);
typedef int (*StrovaPlugin_RunAnalysisFn)(StrovaPluginInstance* instance, const StrovaPluginAnalysisContext* context, StrovaPluginTextBuffer* out_result_text);
typedef int (*StrovaPlugin_ApplyBrushEffectFn)(StrovaPluginInstance* instance, const StrovaPluginBrushEffectContext* context, StrovaPluginTextBuffer* out_effect_json);
typedef int (*StrovaPlugin_ApplyStrokeEffectFn)(StrovaPluginInstance* instance, const StrovaPluginStrokeEffectContext* context, StrovaPluginTextBuffer* out_effect_json);
typedef int (*StrovaPlugin_RunExportPassFn)(StrovaPluginInstance* instance, const StrovaPluginExportPassContext* context, StrovaPluginTextBuffer* out_effect_json);
typedef int (*StrovaPlugin_RunDocumentValidatorFn)(StrovaPluginInstance* instance, const StrovaPluginDocumentValidatorContext* context, StrovaPluginTextBuffer* out_result_text);

#ifdef __cplusplus
}
#endif
