#ifndef RUNTIME_CONFIG_H
#define RUNTIME_CONFIG_H

#include "sentry_user.h"

#include <godot_cpp/classes/config_file.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/string.hpp>

using namespace godot;

class RuntimeConfig : public RefCounted {
	GDCLASS(RuntimeConfig, RefCounted)
private:
	String conf_path;
	Ref<ConfigFile> conf;

	// Cached values.
	Ref<SentryUser> user;
	String installation_id;

protected:
	static void _bind_methods() {}

public:
	Ref<SentryUser> get_user() const { return user; }
	void set_user(const Ref<SentryUser> &p_user);

	String get_installation_id() const { return installation_id; }
	void set_installation_id(const String &p_id);

	void load_file(const String &p_conf_path);

	RuntimeConfig();
};

#endif // RUNTIME_CONFIG_H