#!/usr/bin/env python
import os
import sys
import subprocess
from functools import partial

# *** Setting.

VERSION = "0.6.1"
COMPATIBILITY_MINIMUM = "4.4"

BIN_DIR = "project/addons/sentry/bin"


def run_cmd(**kwargs):
    """Run command in a subprocess and return its exit code."""
    result = subprocess.run(
        kwargs["args"],
        check=True,
    )
    return result.returncode


# *** Generate version header.

print("Generating SDK version header...")

git_sha = "unknown"
try:
    cmd = ["git", "rev-parse", "--short", "HEAD"]
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE)
    git_sha = proc.communicate()[0].strip().decode("utf-8")
except:
    pass

version_header_content = """/* DO NOT EDIT - generated by SConstruct */
#ifndef SENTRY_GODOT_SDK_VERSION_GEN_H
#define SENTRY_GODOT_SDK_VERSION_GEN_H

#define SENTRY_GODOT_SDK_VERSION \"{version}+{git_sha}\"

#endif // SENTRY_GODOT_SDK_VERSION_GEN_H
""".format(version=VERSION, git_sha=git_sha)

if not os.path.exists("src/gen/"):
    os.mkdir("src/gen/")
with open("src/gen/sdk_version.gen.h", "w") as f:
    f.write(version_header_content)


# *** Build godot-cpp.

print("Reading godot-cpp build configuration...")
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
        ["modules/sentry-native/install/lib/sentry.lib",
            BIN_DIR + "/windows/crashpad_handler.exe"],
        ["modules/sentry-native/src/"],
        [
            build_sentry_native,
            Copy(
                BIN_DIR + "/windows/crashpad_handler.exe",
                "modules/sentry-native/install/bin/crashpad_handler.exe",
            ),
        ],
    )

if env["platform"] in ["linux", "macos", "windows"]:
    # Force sentry-native to be built sequential to godot-cpp (not in parallel)
    Depends(sentry_native, "modules/godot-cpp")
    Default(sentry_native)
    Clean(sentry_native, ["modules/sentry-native/build", "modules/sentry-native/install"])

# Include relative to project source root.
env.Append(CPPPATH=["src/"])

# Include sentry-native libs (static).
if env["platform"] in ["linux", "macos", "windows"]:
    env.Append(CPPDEFINES=["SENTRY_BUILD_STATIC", "NATIVE_SDK"])
    env.Append(CPPPATH=["modules/sentry-native/include"])
    env.Append(LIBPATH=["modules/sentry-native/install/lib/"])

    sn_targets = []
    sn_sources = ["modules/sentry-native/src/"]

    def add_target(lib_name):
        env.Append(LIBS=[lib_name])
        if env["platform"] == "windows":
            sn_targets.append("modules/sentry-native/install/lib/" + lib_name + ".lib")
            sn_targets.append("modules/sentry-native/install/lib/" + lib_name + ".pdb")
        else:
            sn_targets.append("modules/sentry-native/install/lib/lib" + lib_name + ".a")

    add_target("sentry")
    add_target("crashpad_client")
    add_target("crashpad_handler_lib")
    add_target("crashpad_minidump")
    add_target("crashpad_snapshot")
    add_target("crashpad_tools")
    add_target("crashpad_util")
    add_target("mini_chromium")

    # Include additional platform-specific libs.
    if env["platform"] == "windows":
        add_target("crashpad_compat")
        env.Append(
            LIBS=[
                "winhttp",
                "advapi32",
                "DbgHelp",
                "Version",
            ]
        )
    elif env["platform"] == "linux":
        add_target("crashpad_compat")
        env.Append(
            LIBS=[
                "curl",
                "atomic"
            ]
        )
    elif env["platform"] == "macos":
        env.Append(
            LIBS=[
                "curl",
            ]
        )

    build_actions = []
    dest_dir = BIN_DIR + "/" + env["platform"]

    if env["platform"] == "windows":
        build_actions.append(
            partial(run_cmd, args=["powershell", "scripts/build-sentry-native.ps1"])
        ),
        build_actions.append(
            Copy(
                dest_dir + "/crashpad_handler.exe",
                "modules/sentry-native/install/bin/crashpad_handler.exe",
            )
        )
        build_actions.append(
            Copy(
                dest_dir + "/crashpad_handler.pdb",
                "modules/sentry-native/install/bin/crashpad_handler.pdb",
            )
        )
        sn_targets.append(dest_dir + "/crashpad_handler.exe")
        sn_targets.append(dest_dir + "/crashpad_handler.pdb")
    else:
        # TODO: macOS needs to use a different SDK.
        build_actions.append(
            partial(run_cmd, args=[
                "sh", "scripts/build-sentry-native.sh",
                "--macos-deployment-target", env["macos_deployment_target"]
            ])
        ),
        build_actions.append(
            Copy(
                dest_dir + "/crashpad_handler",
                "modules/sentry-native/install/bin/crashpad_handler",
            )
        )
        sn_targets.append(dest_dir + "/crashpad_handler")

    sentry_native = env.Command(sn_targets, sn_sources, build_actions)

    # Force sentry-native to be built sequential to godot-cpp (not in parallel).
    Depends(sentry_native, "modules/godot-cpp")

    Default(sentry_native)
    Clean(sentry_native, ["modules/sentry-native/build", "modules/sentry-native/install"])


# *** Build GDExtension library.

# Include relative to project source root.
env.Append(CPPPATH=["src/"])

# Source files to compile.
sources = Glob("src/*.cpp")
sources += Glob("src/editor/*.cpp")
sources += Glob("src/sentry/*.cpp")
sources += Glob("src/sentry/processing/*.cpp")
sources += Glob("src/sentry/util/*.cpp")
# Compile sentry-native code only on respective platforms.
if env["platform"] in ["linux", "windows", "macos"]:
    sources += Glob("src/sentry/native/*.cpp")
elif env["platform"] == "android":
    sources += Glob("src/sentry/android/*.cpp")

# Generate documentation data.
if env["target"] in ["editor", "template_debug"]:
    try:
        doc_data = env.GodotCPPDocData(
            "src/gen/doc_data.gen.cpp", source=Glob("doc_classes/*.xml"))
        sources.append(doc_data)
    except AttributeError:
        print("Not including class reference as we're targeting a pre-4.3 baseline.")

build_type = "release" if env["target"] == "template_release" else "debug"

if env["platform"] == "macos":
    library = env.SharedLibrary(
        "{bin}/{platform}/libsentry.{platform}.{build_type}.framework/libsentry.{platform}.{build_type}".format(
            bin=BIN_DIR,
            platform=env["platform"],
            build_type=build_type,
        ),
        source=sources,
    )
else:
    library = env.SharedLibrary(
        "{bin}/{platform}/libsentry.{platform}.{build_type}.{arch}{shlib_suffix}".format(
            bin=BIN_DIR,
            platform=env["platform"],
            build_type=build_type,
            arch=env["arch"],
            shlib_suffix=env["SHLIBSUFFIX"],
        ),
        source=sources,
    )

Default(library)


# *** Deploy extension manifest.

manifest = env.Substfile(
    target="{bin}/sentry.gdextension".format(
        bin=BIN_DIR,
    ),
    source="src/manifest.gdextension",
    SUBST_DICT={
        "{compatibility_minimum}": COMPATIBILITY_MINIMUM
    },
)

Default(manifest)


# *** Create symbolic link from project addons dir to gdUnit4 testing framework submodule.

def symlink(target, source, env):
    # Note: parameter `target` is a list of build targets.
    assert len(target) == 1
    assert len(source) == 1
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
            print("WARNING: Failed to create NTFS junction for gdUnit4: ", str(e))
    else:
        # Create symlink.
        src = os.path.relpath(src, os.path.dirname(dst))
        os.symlink(src, dst)
    return 0


gdunit_symlink = env.Command(
    "project/addons/gdUnit4",
    "modules/gdUnit4/addons/gdUnit4",
    [
        symlink,
    ],
)

Default(gdunit_symlink)
