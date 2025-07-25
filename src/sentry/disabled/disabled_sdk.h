#ifndef DISABLED_SDK_H
#define DISABLED_SDK_H

#include "disabled_event.h"
#include "sentry/internal_sdk.h"

namespace sentry {

// Internal SDK that does nothing.
class DisabledSDK : public InternalSDK {
	virtual void set_context(const String &p_key, const Dictionary &p_value) override {}
	virtual void remove_context(const String &p_key) override {}

	virtual void set_tag(const String &p_key, const String &p_value) override {}
	virtual void remove_tag(const String &p_key) override {}

	virtual void set_user(const Ref<SentryUser> &p_user) override {}
	virtual void remove_user() override {}

	virtual void add_breadcrumb(const String &p_message, const String &p_category, Level p_level,
			const String &p_type = "default", const Dictionary &p_data = Dictionary()) override {}

	virtual String capture_message(const String &p_message, Level p_level = sentry::LEVEL_INFO) override { return ""; }
	virtual String get_last_event_id() override { return ""; }

	virtual Ref<SentryEvent> create_event() override { return memnew(DisabledEvent); }
	virtual String capture_event(const Ref<SentryEvent> &p_event) override { return ""; }

	virtual void add_attachment(const Ref<SentryAttachment> &p_attachment) override {}

	virtual void initialize(const PackedStringArray &p_global_attachments) override {}
};

} // namespace sentry

#endif // DISABLED_SDK_H
