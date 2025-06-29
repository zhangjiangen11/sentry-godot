#include "native_sdk.h"

#include "sentry.h"
#include "sentry/common_defs.h"
#include "sentry/contexts.h"
#include "sentry/level.h"
#include "sentry/native/native_event.h"
#include "sentry/native/native_util.h"
#include "sentry/util/print.h"
#include "sentry/util/screenshot.h"
#include "sentry/view_hierarchy.h"
#include "sentry_options.h"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/display_server.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <regex>

#ifdef DEBUG_ENABLED
#include <godot_cpp/classes/time.hpp>
#endif

namespace {

void sentry_event_set_context(sentry_value_t p_event, const char *p_context_name, const Dictionary &p_context) {
	ERR_FAIL_COND(sentry_value_get_type(p_event) != SENTRY_VALUE_TYPE_OBJECT);
	ERR_FAIL_COND(strlen(p_context_name) == 0);

	if (p_context.is_empty()) {
		return;
	}

	sentry_value_t contexts = sentry_value_get_by_key(p_event, "contexts");
	if (sentry_value_is_null(contexts)) {
		contexts = sentry_value_new_object();
		sentry_value_set_by_key(p_event, "contexts", contexts);
	}

	// Check if context exists and update or add it.
	sentry_value_t ctx = sentry_value_get_by_key(contexts, p_context_name);
	if (!sentry_value_is_null(ctx)) {
		// If context exists, update it with new values.
		const Array &updated_keys = p_context.keys();
		for (int i = 0; i < updated_keys.size(); i++) {
			const String &key = updated_keys[i];
			sentry_value_set_by_key(ctx, key.utf8(), sentry::native::variant_to_sentry_value(p_context[key]));
		}
	} else {
		// If context doesn't exist, add it.
		sentry_value_set_by_key(contexts, p_context_name, sentry::native::variant_to_sentry_value(p_context));
	}
}

void _save_screenshot(const Ref<SentryEvent> &p_event) {
	if (!SentryOptions::get_singleton()->is_attach_screenshot_enabled()) {
		return;
	}

	static int32_t last_screenshot_frame = 0;
	int32_t current_frame = Engine::get_singleton()->get_frames_drawn();
	if (current_frame == last_screenshot_frame) {
		// Screenshot already exists for this frame — nothing to do.
		return;
	}
	last_screenshot_frame = current_frame;

	String screenshot_path = "user://" SENTRY_SCREENSHOT_FN;
	DirAccess::remove_absolute(screenshot_path);

	if (!DisplayServer::get_singleton() || DisplayServer::get_singleton()->get_name() == "headless") {
		return;
	}

	if (p_event->get_level() < SentryOptions::get_singleton()->get_screenshot_level()) {
		// This check needs to happen after we remove the outdated screenshot file from the drive.
		return;
	}

	if (SentryOptions::get_singleton()->get_before_capture_screenshot().is_valid()) {
		Variant result = SentryOptions::get_singleton()->get_before_capture_screenshot().call(p_event);
		if (result.get_type() != Variant::BOOL) {
			// Note: Using PRINT_ONCE to avoid feedback loop in case of error event.
			ERR_PRINT_ONCE("Sentry: before_capture_screenshot callback failed: expected a boolean return value");
			return;
		}
		if (result.operator bool() == false) {
			sentry::util::print_debug("cancelled screenshot: before_capture_screenshot returned false");
			return;
		}
	}

	sentry::util::print_debug("taking screenshot");

	PackedByteArray buffer = sentry::util::take_screenshot();
	Ref<FileAccess> f = FileAccess::open(screenshot_path, FileAccess::WRITE);
	if (f.is_valid()) {
		f->store_buffer(buffer);
		f->flush();
		f->close();
	} else {
		sentry::util::print_error("failed to save ", screenshot_path);
	}
}

inline void _save_view_hierarchy() {
	if (!SentryOptions::get_singleton()->is_attach_scene_tree_enabled()) {
		return;
	}

#ifdef DEBUG_ENABLED
	uint64_t start = Time::get_singleton()->get_ticks_usec();
#endif

	String path = "user://" SENTRY_VIEW_HIERARCHY_FN;
	DirAccess::remove_absolute(path);

	if (OS::get_singleton()->get_thread_caller_id() != OS::get_singleton()->get_main_thread_id()) {
		sentry::util::print_debug("skipping scene tree capture - can only be performed on the main thread");
		return;
	}

	String json_content = sentry::build_view_hierarchy_json();
	Ref<FileAccess> f = FileAccess::open(path, FileAccess::WRITE);
	if (f.is_valid()) {
		f->store_string(json_content);
		f->flush();
		f->close();
	} else {
		sentry::util::print_error("failed to save ", path);
	}

#ifdef DEBUG_ENABLED
	uint64_t end = Time::get_singleton()->get_ticks_usec();
	sentry::util::print_debug("capturing scene tree data took ", end - start, " usec");
#endif
}

inline void _inject_contexts(sentry_value_t p_event) {
	HashMap<String, Dictionary> contexts = sentry::contexts::make_event_contexts();
	for (const auto &kv : contexts) {
		sentry_event_set_context(p_event, kv.key.utf8(), kv.value);
	}
}

sentry_value_t _handle_before_send(sentry_value_t event, void *hint, void *closure) {
	sentry::util::print_debug("handling before_send");
	Ref<NativeEvent> event_obj = memnew(NativeEvent(event, false));
	_save_screenshot(event_obj);
	_save_view_hierarchy();
	_inject_contexts(event);
	if (const Callable &before_send = SentryOptions::get_singleton()->get_before_send(); before_send.is_valid()) {
		Ref<NativeEvent> processed = before_send.call(event_obj);
		if (processed.is_valid() && processed != event_obj) {
			// Note: Using PRINT_ONCE to avoid feedback loop in case of error event.
			ERR_PRINT_ONCE("Sentry: before_send callback must return the same event object or null.");
			return event;
		}
		if (processed.is_null()) {
			// Discard event.
			sentry::util::print_debug("event discarded by before_send callback: ", event_obj->get_id());
			sentry_value_decref(event);
			return sentry_value_new_null();
		}
		sentry::util::print_debug("event processed by before_send callback: ", event_obj->get_id());
	}
	return event;
}

sentry_value_t _handle_on_crash(const sentry_ucontext_t *uctx, sentry_value_t event, void *closure) {
	sentry::util::print_debug("handling on_crash");
	Ref<NativeEvent> event_obj = memnew(NativeEvent(event, true));
	_save_screenshot(event_obj);
	_save_view_hierarchy();
	_inject_contexts(event);
	if (const Callable &before_send = SentryOptions::get_singleton()->get_before_send(); before_send.is_valid()) {
		Ref<NativeEvent> processed = before_send.call(event_obj);
		if (processed.is_valid() && processed != event_obj) {
			// Note: Using PRINT_ONCE to avoid feedback loop in case of error event.
			ERR_PRINT_ONCE("Sentry: before_send callback must return the same event object or null.");
			return event;
		}
		if (processed.is_null()) {
			// Discard event.
			sentry::util::print_debug("event discarded by before_send callback: ", event_obj->get_id());
			sentry_value_decref(event);
			return sentry_value_new_null();
		}
		sentry::util::print_debug("event processed by before_send callback: ", event_obj->get_id());
	}
	return event;
}

void _log_native_message(sentry_level_t level, const char *message, va_list args, void *userdata) {
	char initial_buffer[512];
	va_list args_copy;
	va_copy(args_copy, args);

	int required = vsnprintf(initial_buffer, sizeof(initial_buffer), message, args);
	if (required < 0) {
		va_end(args_copy);
		ERR_FAIL_MSG("Sentry: Error fomatting message");
	}

	char *buffer = initial_buffer;
	if (required >= sizeof(initial_buffer)) {
		buffer = (char *)malloc(required + 1);
		if (buffer) {
			int new_required = vsnprintf(buffer, required + 1, message, args_copy);
			if (new_required < 0) {
				free(buffer);
				buffer = initial_buffer;
			}
		} else {
			buffer = initial_buffer;
		}
	}
	va_end(args_copy);

	bool accepted = true;

	// Filter out warnings about missing attachment files that may not exist in some scenarios.
	static auto pattern = std::regex{
		R"(^failed to read envelope item from \".*?(screenshot\.jpg|view-hierarchy\.json)\")"
	};
	if (std::regex_search(buffer, buffer + required, pattern)) {
		accepted = false;
	}

	if (accepted) {
		sentry::util::print(sentry::native::native_to_level(level), String(buffer));
	}

	if (buffer != initial_buffer) {
		free(buffer);
	}
}

inline String _uuid_as_string(sentry_uuid_t p_uuid) {
	char str[37];
	sentry_uuid_as_string(&p_uuid, str);
	return str;
}

} // unnamed namespace

namespace sentry {

void NativeSDK::set_context(const String &p_key, const Dictionary &p_value) {
	ERR_FAIL_COND(p_key.is_empty());
	sentry_set_context(p_key.utf8(), sentry::native::variant_to_sentry_value(p_value));
}

void NativeSDK::remove_context(const String &p_key) {
	ERR_FAIL_COND(p_key.is_empty());
	sentry_remove_context(p_key.utf8());
}

void NativeSDK::set_tag(const String &p_key, const String &p_value) {
	ERR_FAIL_COND(p_key.is_empty());
	sentry_set_tag(p_key.utf8(), p_value.utf8());
}

void NativeSDK::remove_tag(const String &p_key) {
	ERR_FAIL_COND(p_key.is_empty());
	sentry_remove_tag(p_key.utf8());
}

void NativeSDK::set_user(const Ref<SentryUser> &p_user) {
	ERR_FAIL_NULL(p_user);

	sentry_value_t user_data = sentry_value_new_object();

	if (!p_user->get_id().is_empty()) {
		sentry_value_set_by_key(user_data, "id",
				sentry_value_new_string(p_user->get_id().utf8()));
	}
	if (!p_user->get_username().is_empty()) {
		sentry_value_set_by_key(user_data, "username",
				sentry_value_new_string(p_user->get_username().utf8()));
	}
	if (!p_user->get_email().is_empty()) {
		sentry_value_set_by_key(user_data, "email",
				sentry_value_new_string(p_user->get_email().utf8()));
	}
	if (!p_user->get_ip_address().is_empty()) {
		sentry_value_set_by_key(user_data, "ip_address",
				sentry_value_new_string(p_user->get_ip_address().utf8()));
	}
	sentry_set_user(user_data);
}

void NativeSDK::remove_user() {
	sentry_remove_user();
}

void NativeSDK::add_breadcrumb(const String &p_message, const String &p_category, Level p_level,
		const String &p_type, const Dictionary &p_data) {
	sentry_value_t crumb = sentry_value_new_breadcrumb(p_type.utf8().ptr(), p_message.utf8().ptr());
	sentry_value_set_by_key(crumb, "category", sentry_value_new_string(p_category.utf8().ptr()));
	sentry_value_set_by_key(crumb, "level", sentry_value_new_string(sentry::level_as_cstring(p_level)));
	sentry_value_set_by_key(crumb, "data", sentry::native::variant_to_sentry_value(p_data));
	sentry_add_breadcrumb(crumb);
}

String NativeSDK::capture_message(const String &p_message, Level p_level) {
	sentry_value_t event = sentry_value_new_message_event(
			native::level_to_native(p_level),
			"", // logger
			p_message.utf8().get_data());

	sentry_uuid_t uuid = sentry_capture_event(event);
	last_uuid.store(uuid, std::memory_order_release);
	return _uuid_as_string(uuid);
}

String NativeSDK::get_last_event_id() {
	return _uuid_as_string(last_uuid.load(std::memory_order_acquire));
}

Ref<SentryEvent> NativeSDK::create_event() {
	sentry_value_t event_value = sentry_value_new_event();
	Ref<SentryEvent> event = memnew(NativeEvent(event_value, false));
	return event;
}

String NativeSDK::capture_event(const Ref<SentryEvent> &p_event) {
	ERR_FAIL_COND_V_MSG(p_event.is_null(), _uuid_as_string(sentry_uuid_nil()), "Sentry: Can't capture event - event object is null.");
	NativeEvent *native_event = Object::cast_to<NativeEvent>(p_event.ptr());
	ERR_FAIL_NULL_V(native_event, _uuid_as_string(sentry_uuid_nil())); // Sanity check - this should never happen.
	sentry_value_t event = native_event->get_native_value();
	sentry_value_incref(event); // Keep ownership.
	sentry_uuid_t uuid = sentry_capture_event(event);
	last_uuid.store(uuid, std::memory_order_release);
	return _uuid_as_string(uuid);
}

void NativeSDK::initialize(const PackedStringArray &p_global_attachments) {
	ERR_FAIL_NULL(OS::get_singleton());
	ERR_FAIL_NULL(ProjectSettings::get_singleton());

	sentry_options_t *options = sentry_options_new();

	sentry_options_set_dsn(options, SentryOptions::get_singleton()->get_dsn().utf8());
	sentry_options_set_database_path(options, (OS::get_singleton()->get_user_data_dir() + "/sentry").utf8());
	sentry_options_set_debug(options, SentryOptions::get_singleton()->is_debug_enabled());
	sentry_options_set_release(options, SentryOptions::get_singleton()->get_release().utf8());
	sentry_options_set_dist(options, SentryOptions::get_singleton()->get_dist().utf8());
	sentry_options_set_environment(options, SentryOptions::get_singleton()->get_environment().utf8());
	sentry_options_set_sample_rate(options, SentryOptions::get_singleton()->get_sample_rate());
	sentry_options_set_max_breadcrumbs(options, SentryOptions::get_singleton()->get_max_breadcrumbs());
	sentry_options_set_sdk_name(options, "sentry.native.godot");

	// Establish handler path.
	String handler_fn;
	String platform_dir;
	String export_subdir;
#ifdef LINUX_ENABLED
	handler_fn = "crashpad_handler";
	platform_dir = "linux";
#elif MACOS_ENABLED
	handler_fn = "crashpad_handler";
	platform_dir = "macos";
	export_subdir = "../Frameworks";
#elif WINDOWS_ENABLED
	handler_fn = "crashpad_handler.exe";
	platform_dir = "windows";
#else
	ERR_PRINT("Sentry: Internal Error: NativeSDK should not be initialized on an unsupported platform (this should not happen).");
#endif
	String exe_dir = OS::get_singleton()->get_executable_path().get_base_dir();
	String handler_path = exe_dir.path_join(export_subdir).path_join(handler_fn);
	if (!FileAccess::file_exists(handler_path)) {
		const String addon_bin_dir = "res://addons/sentry/bin/";
		handler_path = ProjectSettings::get_singleton()->globalize_path(
				addon_bin_dir.path_join(platform_dir).path_join(handler_fn));
	}
	if (FileAccess::file_exists(handler_path)) {
		sentry_options_set_handler_path(options, handler_path.utf8());
	} else {
		ERR_PRINT(vformat("Sentry: Failed to locate crash handler (crashpad) - backend disabled (%s)", handler_path));
		sentry_options_set_backend(options, NULL);
	}

	for (const String &path : p_global_attachments) {
		sentry::util::print_debug("adding attachment \"", path, "\"");
		if (path.ends_with(SENTRY_VIEW_HIERARCHY_FN)) {
			sentry_options_add_view_hierarchy(options, path.utf8());
		} else {
			sentry_options_add_attachment(options, path.utf8());
		}
	}

	// Hooks.
	sentry_options_set_before_send(options, _handle_before_send, NULL);
	sentry_options_set_on_crash(options, _handle_on_crash, NULL);
	sentry_options_set_logger(options, _log_native_message, NULL);

	int err = sentry_init(options);
	initialized = (err == 0);

	if (err != 0) {
		ERR_PRINT("Sentry: Failed to initialize native SDK. Error code: " + itos(err));
	}
}

NativeSDK::~NativeSDK() {
	sentry_close();
}

} //namespace sentry
