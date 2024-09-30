#ifndef SETTINGS_H
#define SETTINGS_H

#include <godot_cpp/core/property_info.hpp>
#include <godot_cpp/variant/char_string.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/variant.hpp>

class Settings {
private:
	static Settings *singleton;

	godot::CharString dsn;
	godot::CharString release;
	bool debug_printing = false;
	double sample_rate = 1.0;

	void _define_setting(const godot::String &p_setting, const godot::Variant &p_default);
	void _define_setting(const godot::PropertyInfo &p_info, const godot::Variant &p_default);
	void _define_project_settings();
	void _load_config();

public:
	static Settings *get_singleton() { return singleton; }

	godot::CharString get_dsn() const { return dsn; }
	godot::CharString get_release() const { return release; }
	bool is_debug_printing_enabled() const { return debug_printing; }
	double get_sample_rate() const { return sample_rate; }

	Settings();
	~Settings();
};

#endif // SETTINGS_H
