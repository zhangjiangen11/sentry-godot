package io.sentry.godotplugin

import android.util.Log
import io.sentry.Attachment
import io.sentry.Breadcrumb
import io.sentry.Hint
import io.sentry.Sentry
import io.sentry.SentryEvent
import io.sentry.SentryLevel
import io.sentry.SentryOptions
import io.sentry.android.core.SentryAndroid
import io.sentry.protocol.Message
import io.sentry.protocol.SentryException
import io.sentry.protocol.SentryStackFrame
import io.sentry.protocol.SentryStackTrace
import io.sentry.protocol.User
import org.godotengine.godot.Dictionary
import org.godotengine.godot.Godot
import org.godotengine.godot.plugin.GodotPlugin
import org.godotengine.godot.plugin.UsedByGodot
import org.godotengine.godot.variant.Callable
import java.io.File
import kotlin.random.Random


@Suppress("unused")
class SentryAndroidGodotPlugin(godot: Godot) : GodotPlugin(godot) {
    companion object {
        private const val TAG = "sentry-godot"
    }

    private val eventsByHandle = object : ThreadLocal<MutableMap<Int, SentryEvent>>() {
        override fun initialValue(): MutableMap<Int, SentryEvent> {
            return mutableMapOf()
        }
    }

    private val exceptionsByHandle = object : ThreadLocal<MutableMap<Int, SentryException>>() {
        override fun initialValue(): MutableMap<Int, SentryException> {
            return mutableMapOf()
        }
    }

    private fun getEvent(eventHandle: Int): SentryEvent? {
        val event: SentryEvent? = eventsByHandle.get()?.get(eventHandle)
        if (event == null) {
            Log.e(TAG, "Internal Error -- SentryEvent not found: $eventHandle")
        }
        return event
    }

    private fun getException(exceptionHandle: Int): SentryException? {
        val exception: SentryException? = exceptionsByHandle.get()?.get(exceptionHandle)
        if (exception == null) {
            Log.e(TAG, "Internal Error -- SentryException not found: $exceptionHandle")
        }
        return exception
    }

    private fun registerEvent(event: SentryEvent): Int {
        val eventsMap = eventsByHandle.get() ?: run {
            Log.e(TAG, "Internal Error -- eventsByHandle is null")
            return 0
        }

        var handle = Random.nextInt()
        while (eventsMap.containsKey(handle)) {
            handle = Random.nextInt()
        }

        eventsMap[handle] = event
        return handle
    }

    private fun registerException(exception: SentryException): Int {
        val exceptionsMap = exceptionsByHandle.get() ?: run {
            Log.e(TAG, "Internal Error -- exceptionsByHandle is null")
            return 0
        }

        var handle = Random.nextInt()
        while (exceptionsMap.containsKey(handle)) {
            handle = Random.nextInt()
        }

        exceptionsMap[handle] = exception
        return handle
    }

    override fun getPluginName(): String {
        return "SentryAndroidGodotPlugin"
    }

    @UsedByGodot
    fun initialize(
        beforeSendHandlerId: Long,
        dsn: String,
        debug: Boolean,
        release: String,
        dist: String,
        environment: String,
        sampleRate: Float,
        maxBreadcrumbs: Int,
    ) {
        Log.v(TAG, "Initializing Sentry Android")
        SentryAndroid.init(godot.getActivity()!!.applicationContext) { options ->
            options.dsn = dsn.ifEmpty { null }
            options.isDebug = debug
            options.release = release.ifEmpty { null }
            options.dist = dist.ifEmpty { null }
            options.environment = environment.ifEmpty { null }
            options.sampleRate = sampleRate.toDouble()
            options.maxBreadcrumbs = maxBreadcrumbs
            options.sdkVersion?.name = "sentry.java.android.godot"
            options.nativeSdkName = "sentry.native.android.godot"
            options.beforeSend =
                SentryOptions.BeforeSendCallback { event: SentryEvent, hint: Hint ->
                    Log.v(TAG, "beforeSend: ${event.eventId} isCrashed: ${event.isCrashed}")
                    val handle: Int = registerEvent(event)
                    Callable.call(beforeSendHandlerId, "before_send", handle)
                    eventsByHandle.get()?.remove(handle) // Returns the event or null if it was discarded.
                }
            }
    }

    @UsedByGodot
    fun addFileAttachment(path: String, filename: String, contentType: String, attachmentType: String) {
        val attachment = Attachment(
            path,
            filename.ifEmpty { File(path).name },
            contentType.ifEmpty { null },
            attachmentType.ifEmpty { null },
            false
        )
        Sentry.getGlobalScope().addAttachment(attachment)
    }

    @UsedByGodot
    fun addBytesAttachment(bytes: ByteArray, filename: String, contentType: String, attachmentType: String) {
        val attachment = Attachment(
            bytes,
            filename,
            contentType.ifEmpty { null },
            attachmentType.ifEmpty { null },
            false
        )
        Sentry.getGlobalScope().addAttachment(attachment)
    }

    @UsedByGodot
    fun setContext(key: String, value: Dictionary) {
        Sentry.getGlobalScope().setContexts(key, value)
    }

    @UsedByGodot
    fun removeContext(key: String) {
        Sentry.getGlobalScope().removeContexts(key)
    }

    @UsedByGodot
    fun setTag(key: String, value: String) {
        Sentry.setTag(key, value)
    }

    @UsedByGodot
    fun removeTag(key: String) {
        Sentry.removeTag(key)
    }

    @UsedByGodot
    fun setUser(id: String, userName: String, email: String, ipAddress: String) {
        val user = User()
        if (id.isNotEmpty()) {
            user.id = id
        }
        if (userName.isNotEmpty()) {
            user.username = userName
        }
        if (email.isNotEmpty()) {
            user.email = email
        }
        if (ipAddress.isNotEmpty()) {
            user.ipAddress = ipAddress
        }
        Sentry.setUser(user)
    }

    @UsedByGodot
    fun removeUser() {
        Sentry.setUser(null)
    }

    @UsedByGodot
    fun addBreadcrumb(
        message: String,
        category: String,
        level: Int,
        type: String,
        data: Dictionary
    ) {
        val crumb = Breadcrumb()
        crumb.message = message
        crumb.category = category
        crumb.level = level.toSentryLevel()
        crumb.type = type

        for ((k, v) in data) {
            crumb.data[k] = v
        }

        Sentry.addBreadcrumb(crumb)
    }

    @UsedByGodot
    fun captureMessage(message: String, level: Int): String {
        val id = Sentry.captureMessage(message, level.toSentryLevel())
        return id.toString()
    }

    @UsedByGodot
    fun getLastEventId(): String {
        return Sentry.getLastEventId().toString()
    }

    @UsedByGodot
    fun createEvent(): Int {
        val event = SentryEvent()
        val handle = registerEvent(event)
        return handle
    }

    @UsedByGodot
    fun releaseEvent(eventHandle: Int) {
        val eventsMap = eventsByHandle.get() ?: run {
            Log.e(TAG, "Internal Error -- eventsByHandle is null")
            return
        }

        eventsMap.remove(eventHandle)
    }

    @UsedByGodot
    fun captureEvent(eventHandle: Int): String {
        val event: SentryEvent = getEvent(eventHandle) ?: run {
            Log.e(TAG, "Failed to capture event: $eventHandle")
            return ""
        }
        val id = Sentry.captureEvent(event)
        return id.toString()
    }

    @UsedByGodot
    fun eventGetId(eventHandle: Int): String {
        val id = getEvent(eventHandle)?.eventId ?: return ""
        return id.toString()
    }

    @UsedByGodot
    fun eventSetMessage(eventHandle: Int, message: String) {
        val event: SentryEvent = getEvent(eventHandle) ?: return
        val sentryMessage = Message()
        sentryMessage.formatted = message
        event.message = sentryMessage
    }

    @UsedByGodot
    fun eventGetMessage(eventHandle: Int): String {
        return getEvent(eventHandle)?.message?.formatted ?: return ""
    }

    @UsedByGodot
    fun eventSetTimestamp(eventHandle: Int, timestamp: String) {
        val event: SentryEvent = getEvent(eventHandle) ?: return
        try {
            event.timestamp = timestamp.parseTimestamp()
        } catch (_: Exception) {
            Log.e(TAG, "Failed to parse timestamp: $timestamp")
        }
    }

    @UsedByGodot
    fun eventGetTimestamp(eventHandle: Int): String {
        val event: SentryEvent = getEvent(eventHandle) ?: return ""
        return event.timestamp?.toRfc3339() ?: ""
    }

    @UsedByGodot
    fun eventGetPlatform(eventHandle: Int): String {
        return getEvent(eventHandle)?.platform ?: return ""
    }

    @UsedByGodot
    fun eventSetLevel(eventHandle: Int, level: Int) {
        getEvent(eventHandle)?.level = level.toSentryLevel()
    }

    @UsedByGodot
    fun eventGetLevel(eventHandle: Int): Int {
        return getEvent(eventHandle)?.level?.toInt() ?: return SentryLevel.ERROR.toInt()
    }

    @UsedByGodot
    fun eventSetLogger(eventHandle: Int, logger: String) {
        getEvent(eventHandle)?.logger = logger
    }

    @UsedByGodot
    fun eventGetLogger(eventHandle: Int): String {
        return getEvent(eventHandle)?.logger ?: return ""
    }

    @UsedByGodot
    fun eventSetRelease(eventHandle: Int, release: String) {
        getEvent(eventHandle)?.release = release
    }

    @UsedByGodot
    fun eventGetRelease(eventHandle: Int): String {
        return getEvent(eventHandle)?.release ?: ""
    }

    @UsedByGodot
    fun eventSetDist(eventHandle: Int, dist: String) {
        getEvent(eventHandle)?.dist = dist
    }

    @UsedByGodot
    fun eventGetDist(eventHandle: Int): String {
        return getEvent(eventHandle)?.dist ?: ""
    }

    @UsedByGodot
    fun eventSetEnvironment(eventHandle: Int, environment: String) {
        getEvent(eventHandle)?.environment = environment
    }

    @UsedByGodot
    fun eventGetEnvironment(eventHandle: Int): String {
        return getEvent(eventHandle)?.environment ?: ""
    }

    @UsedByGodot
    fun eventSetTag(eventHandle: Int, key: String, value: String) {
        getEvent(eventHandle)?.setTag(key, value)
    }

    @UsedByGodot
    fun eventGetTag(eventHandle: Int, key: String): String {
        return getEvent(eventHandle)?.getTag(key) ?: ""
    }

    @UsedByGodot
    fun eventRemoveTag(eventHandle: Int, key: String) {
        getEvent(eventHandle)?.removeTag(key)
    }

    @UsedByGodot
    fun eventMergeContext(eventHandle: Int, key: String, value: Dictionary) {
        val event = getEvent(eventHandle) ?: return

        val existingContext: Any? = event.contexts[key]

        if (existingContext is Dictionary) {
            // Fast path: merge Dictionary directly.
            existingContext.putAll(value)
        } else {
            val existingMap = existingContext as? Map<*, *>
            existingMap?.let {
                // Merge elements from existing context into new context, but only for keys
                // that don't already exist in the new context.
                for ((k, v) in it) {
                    val kStr: String = k as? String ?: continue
                    if (!value.containsKey(kStr)) {
                        value[kStr] = v
                    }
                }
            }
            event.contexts[key] = value
        }

    }

    @UsedByGodot
    fun eventIsCrash(eventHandle: Int): Boolean {
        return getEvent(eventHandle)?.isCrashed == true
    }

    @UsedByGodot
    fun createException(type: String, value: String): Int {
        val exception = SentryException()
        exception.type = type
        exception.value = value
        exception.stacktrace = SentryStackTrace()
        exception.stacktrace?.frames = mutableListOf()
        return registerException(exception)
    }

    @UsedByGodot
    fun releaseException(exceptionHandle: Int) {
        val exceptionsMap = exceptionsByHandle.get()
        if (exceptionsMap == null) {
            Log.e(TAG, "Internal Error -- exceptionsByHandle is null")
            return
        }

        exceptionsMap.remove(exceptionHandle)
    }

    @UsedByGodot
    fun exceptionAppendStackFrame(exceptionHandle: Int, frameData: Dictionary) {
        val exception = getException(exceptionHandle) ?: return
        val frame = SentryStackFrame().apply {
            filename = frameData["filename"] as? String
            function = frameData["function"] as? String
            lineno = frameData["lineno"] as? Int
            isInApp = frameData["in_app"] as? Boolean
            platform = frameData["platform"] as? String

            if (frameData.containsKey("context_line")) {
                contextLine = frameData["context_line"] as? String
                preContext = (frameData["pre_context"] as? Array<*>)?.map { it as String }
                postContext = (frameData["post_context"] as? Array<*>)?.map { it as String }
            }
        }

        exception.stacktrace?.frames?.add(frame)
    }

    @UsedByGodot
    fun eventAddException(eventHandle: Int, exceptionHandle: Int) {
        val event = getEvent(eventHandle) ?: return
        val exception = getException(exceptionHandle) ?: return
        if (event.exceptions == null) {
            event.exceptions = mutableListOf()
        }
        event.exceptions?.add(exception)
    }
}
