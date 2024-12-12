#!/usr/bin/env python
import os
import sys
import subprocess

# Global Setting.
PROJECT_DIR = "project"
EXTENSION_NAME = "sentrysdk"
COMPATIBILITY_MINIMUM = "4.3"

BIN_DIR = "{project_dir}/addons/{extension_name}/bin".format(
    project_dir=PROJECT_DIR,
    extension_name=EXTENSION_NAME)

env = SConscript("modules/godot-cpp/SConstruct")

# *** Build sentry-native.

# TODO: macOS needs to use a different SDK.
if env["platform"] in ["linux", "macos"]:

    def build_sentry_native(target, source, env):
        result = subprocess.run(
            ["sh", "scripts/build-sentry-native.sh"],
            check=True,
        )
        return result.returncode

    crashpad_handler_target = "{bin}/{platform}/crashpad_handler".format(
        bin=BIN_DIR,
        platform=env["platform"]
    )
    sentry_native = env.Command(
        [
            "modules/sentry-native/install/lib/libsentry.a",
            crashpad_handler_target,
        ],
        ["modules/sentry-native/src"],
        [
            build_sentry_native,
            Copy(
                crashpad_handler_target,
                "modules/sentry-native/install/bin/crashpad_handler",
            ),
        ],
    )
elif env["platform"] == "windows":

    def build_sentry_native(target, source, env):
        result = subprocess.run(
            ["powershell", "scripts/build-sentry-native.ps1"],
            check=True,
        )
        return result.returncode

    sentry_native = env.Command(
        ["modules/sentry-native/install/lib/sentry.lib", BIN_DIR + "/windows/crashpad_handler.exe"],
        ["modules/sentry-native/src/"],
        [
            build_sentry_native,
            Copy(
                BIN_DIR + "/windows/crashpad_handler.exe",
                "modules/sentry-native/install/bin/crashpad_handler.exe",
            ),
        ],
    )
Depends(sentry_native, "modules/godot-cpp")  # Force sentry-native to be built sequential to godot-cpp (not in parallel)
Default(sentry_native)
Clean(sentry_native, ["modules/sentry-native/build", "modules/sentry-native/install"])

# Include relative to project source root.
env.Append(CPPPATH=["src/"])

# Include sentry-native libs (static).
if env["platform"] in ["linux", "macos", "windows"]:
    env.Append(CPPDEFINES=["SENTRY_BUILD_STATIC", "NATIVE_SDK"])
    env.Append(CPPPATH=["modules/sentry-native/include"])
    env.Append(LIBPATH=["modules/sentry-native/install/lib/"])
    env.Append(
        LIBS=[
            "sentry",
            "crashpad_client",
            "crashpad_handler_lib",
            "crashpad_minidump",
            "crashpad_snapshot",
            "crashpad_tools",
            "crashpad_util",
            "mini_chromium",
        ]
    )
# Include additional platform-specific libs.
if env["platform"] == "windows":
    env.Append(
        LIBS=[
            "crashpad_compat",
            "winhttp",
            "advapi32",
            "DbgHelp",
            "Version",
        ]
    )
elif env["platform"] == "linux":
    env.Append(
        LIBS=[
            "crashpad_compat",
            "curl",
        ]
    )
elif env["platform"] == "macos":
    env.Append(
        LIBS=[
            "curl",
        ]
    )

# *** Build GDExtension library.

# Source files to compile.
sources = Glob("src/*.cpp")
sources += Glob("src/sentry/*.cpp")
# Compile sentry-native code only on respective platforms.
if env["platform"] in ["linux", "windows", "macos"]:
    sources += Glob("src/sentry/native/*.cpp")

build_type = "release" if env["target"] == "template_release" else "debug"

if env["platform"] == "macos":
    library = env.SharedLibrary(
        "{bin}/{platform}/lib{name}.{platform}.{build_type}.framework/lib{name}.{platform}.{build_type}".format(
            bin=BIN_DIR,
            name=EXTENSION_NAME,
            platform=env["platform"],
            build_type=build_type,
        ),
        source=sources,
    )
else:
    library = env.SharedLibrary(
        "{bin}/{platform}/lib{name}.{platform}.{build_type}.{arch}{shlib_suffix}".format(
            bin=BIN_DIR,
            name=EXTENSION_NAME,
            platform=env["platform"],
            build_type=build_type,
            arch=env["arch"],
            shlib_suffix=env["SHLIBSUFFIX"]
        ),
        source=sources,
    )

Default(library)

# *** Deploy extension manifest.

manifest = env.Substfile(
    target="{bin}/{name}.gdextension".format(
        bin=BIN_DIR,
        name=EXTENSION_NAME,
    ),
    source="src/manifest.gdextension",
    SUBST_DICT={
        "{name}": EXTENSION_NAME,
        "{compatibility_minimum}": COMPATIBILITY_MINIMUM,
    },
)

Default(manifest)

# *** Create symbolic link from project addons dir to gdUnit4 testing framework submodule.

def symlink(target, source, env):
    # Note: parameter `target` is a list of build targets.
    assert(len(target) == 1)
    assert(len(source) == 1)
    dst = str(target[0])
    src = str(source[0])
    if env["platform"] == "windows":
        # Create NTFS junction.
        # Note: Windows requires elevated privileges to create symlinks, so we're creating NTFS junction instead.
        try:
            import _winapi
            _winapi.CreateJunction(src, dst)
        except Exception as e:
            # Don't fail the build if this step fails.
            print("WARNING: Failed to create an NTFS junction for gdUnit4 testing framework: ", str(e))
    else:
        # Create symlink.
        src = os.path.relpath(src, os.path.dirname(dst))
        os.symlink(src, dst)
    return 0

gdunit_symlink = env.Command(
    PROJECT_DIR + "/addons/gdUnit4",
    "modules/gdUnit4/addons/gdUnit4",
    [
        symlink,
    ],
)

Default(gdunit_symlink)
