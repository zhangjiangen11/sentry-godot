#include "sentry_logger.h"

#include "sentry/util/print.h"
#include "sentry_options.h"
#include "sentry_sdk.h"

#include <cstring>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/script.hpp>

namespace {

using GodotErrorType = sentry::GodotErrorType;

#define MAX_LINE_LENGTH 1024

// Godot error strings we look for in the log file.
// A line starting with one of these strings will be parsed as an error.
const char *error_types[] = {
	"ERROR",
	"WARNING",
	"SCRIPT ERROR",
	"SHADER ERROR",
	"USER ERROR",
	"USER WARNING",
	"USER SCRIPT ERROR",
	"USER SHADER ERROR"
};

// Map error strings from the previous array definition to error type enum.
// For example, error_types[3] maps to error_type_as_enum[3].
GodotErrorType error_type_as_enum[] = {
	GodotErrorType::ERROR_TYPE_ERROR,
	GodotErrorType::ERROR_TYPE_WARNING,
	GodotErrorType::ERROR_TYPE_SCRIPT,
	GodotErrorType::ERROR_TYPE_SHADER,
	GodotErrorType::ERROR_TYPE_ERROR,
	GodotErrorType::ERROR_TYPE_WARNING,
	GodotErrorType::ERROR_TYPE_SCRIPT,
	GodotErrorType::ERROR_TYPE_SHADER,
};

const int num_error_types = sizeof(error_types) / sizeof(error_types[0]);

} // unnamed namespace

void SentryLogger::_process_log_file() {
	if (!log_file.is_open()) {
		set_process(false);
		ERR_PRINT_ONCE("Sentry: Internal error: Log file not open. Error logging stopped.");
		return;
	}

	// Reset per-frame counter.
	frame_events = 0;

	// Get limits.
	Ref<SentryLoggerLimits> limits = SentryOptions::get_singleton()->get_logger_limits();
	auto repeated_error_window = std::chrono::milliseconds{ limits->repeated_error_window_ms };
	auto throttle_window = std::chrono::milliseconds{ limits->throttle_window_ms };

	{
		// Throttling: Remove time points outside of the throttling window.
		auto now = std::chrono::high_resolution_clock::now();
		while (event_times.size() && now - event_times.front() >= throttle_window) {
			event_times.pop_front();
		}
	}

	log_file.clear(); // Remove eof flag, so that we can read the next line.
	log_file.seekg(last_pos);

	int num_lines_read = 0;
	char first_line[MAX_LINE_LENGTH];
	char second_line[MAX_LINE_LENGTH];

	// Throttling:
	// - For throttling, we use a sliding window approach. If we reach the limit of events
	//   within the throttling window, we stop registering events until the window opens up again,
	//   while still registering them as breadcrumbs.
	// - We also have a limit on events per frame, and on the number of lines that can be parsed
	//   in each frame. These limits are mainly here to protect the frametime budget.
	while (num_lines_read < limits->parse_lines && log_file.getline(first_line, MAX_LINE_LENGTH)) {
		num_lines_read++;

		for (int i = 0; i < num_error_types; i++) {
			if (strncmp(first_line, error_types[i], strlen(error_types[i])) == 0) {
				if (!log_file.getline(second_line, MAX_LINE_LENGTH)) {
					continue;
				}
				num_lines_read++;

				// Parse error string.
				// See: https://github.com/godotengine/godot/blob/04692d83cb8f61002f18ea1d954df8c558ee84f7/core/io/logger.cpp#L88
				GodotErrorType err_type = error_type_as_enum[i];
				char *rationale = first_line + strlen(error_types[i]) + 2; // +2 to skip ": "
				char func[100];
				char file_part[200];
				int parsed = sscanf(second_line, "   at: %99s (%199[^)])\n", func, file_part);
				if (parsed == 2) {
					// Split file name and line number.
					char *last_colon = strrchr(file_part, ':');
					if (last_colon != NULL) {
						*last_colon = '\0';
						int line = atoi(last_colon + 1);

						// Reject errors based on per-source-line throttling window to prevent
						// repetitive logging caused by loops or recurring errors in each frame.
						// Last log time is tracked for each source line that produced an error.
						SourceLine src_line{ file_part, line };
						TimePoint now = std::chrono::high_resolution_clock::now();
						auto it = source_line_times.find(src_line);
						if (it == source_line_times.end() || now - it->second >= repeated_error_window) {
							_log_error(func, file_part, line, rationale, err_type);
							source_line_times[src_line] = now;
						} else {
							sentry::util::print_debug("error capture was canceled due to throttling for ",
									file_part, " at line ", line, ".");
						}
					}
				}

				break;
			}
		}
		last_pos = log_file.tellg();
	}

	// Seek to the end of file - don't process the rest of the lines.
	log_file.seekg(0, std::ios::end);
}

void SentryLogger::_log_error(const char *p_func, const char *p_file, int p_line, const char *p_rationale, GodotErrorType p_error_type) {
	Ref<SentryLoggerLimits> limits = SentryOptions::get_singleton()->get_logger_limits();
	bool as_breadcrumb = SentryOptions::get_singleton()->should_capture_breadcrumb(p_error_type);
	bool as_event = SentryOptions::get_singleton()->should_capture_event(p_error_type) &&
			frame_events < limits->events_per_frame &&
			event_times.size() < limits->throttle_events;

	if (!as_breadcrumb && !as_event) {
		// Bail out if capture is disabled for this error type.
		return;
	}

	// Debug output.
	if (SentryOptions::get_singleton()->is_debug_enabled()) {
		sentry::util::print_debug(
				"Error logged:\n",
				"   Function: \"", p_func, "\"\n",
				"   File: ", p_file, "\n",
				"   Line: ", p_line, "\n",
				"   Rationale: ", p_rationale, "\n",
				"   Error Type: ", error_types[int(p_error_type)]);
	}

	TimePoint now = std::chrono::high_resolution_clock::now();

	// Capture error as event.
	if (as_event) {
		SentryEvent::StackFrame stack_frame{
			p_file,
			p_func,
			p_line,
			true, // in_app
			"godot" // platform
		};

		// Provide script source code context for script errors if available.
		if (p_error_type == GodotErrorType::ERROR_TYPE_SCRIPT && SentryOptions::get_singleton()->is_logger_include_source_enabled()) {
			// Provide script source code context for script errors if available.
			String context_line;
			PackedStringArray pre_context;
			PackedStringArray post_context;
			bool err = _get_script_context(p_file, p_line, context_line, pre_context, post_context);
			if (!err) {
				stack_frame.context_line = context_line;
				stack_frame.pre_context = pre_context;
				stack_frame.post_context = post_context;
			}
		}

		Ref<SentryEvent> ev = SentrySDK::get_singleton()->create_event();
		ev->set_level(sentry::get_sentry_level_for_godot_error_type(p_error_type));
		ev->add_exception({ error_types[int(p_error_type)], p_rationale, { stack_frame } });
		SentrySDK::get_singleton()->capture_event(ev);

		// For throttling
		frame_events++;
		event_times.push_back(now);
	}

	// Capture error as breadcrumb.
	if (as_breadcrumb) {
		Dictionary data;
		data["function"] = String(p_func);
		data["file"] = String(p_file);
		data["line"] = p_line;
		data["godot_error_type"] = String(error_types[int(p_error_type)]);

		SentrySDK::get_singleton()->add_breadcrumb(
				p_rationale,
				"error",
				sentry::get_sentry_level_for_godot_error_type(p_error_type),
				"error",
				data);
	}
}

void SentryLogger::_trim_error_timepoints() {
	// Clearing the map if it gets too big. Cheap and efficient.
	if (source_line_times.size() > 100) {
		source_line_times.clear();
	}
}

bool SentryLogger::_get_script_context(const String &p_file, int p_line, String &r_context_line, PackedStringArray &r_pre_context, PackedStringArray &r_post_context) const {
	if (p_file.is_empty()) {
		return true;
	}

	Ref<Script> script = ResourceLoader::get_singleton()->load(p_file);

	// ! Note: Script source code context is only automatically provided if GDScript is exported as text (not binary tokens).

	if (script.is_null()) {
		sentry::util::print_error("Failed to load script ", p_file);
		return true;
	}

	String source_code = script->get_source_code();
	if (source_code.is_empty()) {
		sentry::util::print_debug("Script source not available ", p_file.utf8().ptr());
		return true;
	}

	PackedStringArray lines = script->get_source_code().split("\n");
	if (lines.size() < p_line) {
		sentry::util::print_error("Script source is smaller than the referenced line, lineno: ", p_line);
		return true;
	}

	r_context_line = lines[p_line - 1];
	r_pre_context = lines.slice(MAX(p_line - 6, 0), p_line - 1);
	r_post_context = lines.slice(p_line, MIN(p_line + 5, lines.size()));
	return false;
}

void SentryLogger::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			sentry::util::print_debug("starting logger");

			// Periodically remove old error time points to free up memory, if it gets too big.
			trim_timer->connect("timeout", callable_mp(this, &SentryLogger::_trim_error_timepoints));

			_setup();
		} break;
		case NOTIFICATION_EXIT_TREE: {
			sentry::util::print_debug("finishing logger");
			log_file.close();
		} break;
		case NOTIFICATION_PROCESS: {
			// Process log file at the end of the current frame.
			process_log.call_deferred();
		} break;
	}
}

void SentryLogger::_setup() {
	ERR_FAIL_NULL(ProjectSettings::get_singleton());
	ERR_FAIL_NULL(OS::get_singleton());

	bool logging_setting = ProjectSettings::get_singleton()->get_setting("debug/file_logging/enable_file_logging");
	bool logging_setting_pc = ProjectSettings::get_singleton()->get_setting("debug/file_logging/enable_file_logging.pc");
	bool logging_enabled;
#if defined(WINDOWS_ENABLED) || defined(LINUX_ENABLED) || defined(MACOS_ENABLED)
	logging_enabled = logging_setting_pc || logging_setting;
#else
	logging_enabled = logging_setting;
#endif
	ERR_FAIL_COND_MSG(!logging_enabled, "Sentry: Error logger failure - file logging disabled in project settings. Tip: Enable \"debug/file_logging/enable_file_logging\" in the project settings.");

	String log_path = ProjectSettings::get_singleton()->get_setting("debug/file_logging/log_path");
	ERR_FAIL_COND_MSG(log_path.is_empty(), "Sentry: Error logger failure - project settings \"debug/file_logging/log_path\" is not set. Please, assign a valid file path in the project settings.");
	log_path = log_path.replace("user://", OS::get_singleton()->get_user_data_dir() + "/");
	sentry::util::print_debug("opening ", log_path);
	log_file.open(log_path.utf8(), std::ios::in);
	set_process(log_file.is_open());
	ERR_FAIL_COND_MSG(!log_file.is_open(), "Sentry: Error logger failure - couldn't open the log file: " + log_path);
}

SentryLogger::SentryLogger() {
	set_process(false);
	process_log = callable_mp(this, &SentryLogger::_process_log_file);

	trim_timer = memnew(Timer);
	trim_timer->set_one_shot(false);
	trim_timer->set_wait_time(60); // 1 minute
	trim_timer->set_autostart(true);
	add_child(trim_timer);
}
