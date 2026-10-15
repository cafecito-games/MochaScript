#!/usr/bin/env python

import os
import sys

from methods import print_error


libname = "mocha_script"
projectdir = "project"
expected_api = os.path.abspath("api/extension_api.json")
for forbidden_option in ("custom_api_file", "gdextension_dir", "build_profile", "profile", "precision"):
    if forbidden_option in ARGUMENTS:
        print_error("{} is pinned by MochaScript and cannot be overridden".format(forbidden_option))
        sys.exit(1)

local_env = Environment(tools=["default"], PLATFORM="")

customs = []
opts = Variables(customs, ARGUMENTS)
opts.Update(local_env)
local_env["custom_api_file"] = expected_api
Help(opts.GenerateHelpText(local_env))

env = local_env.Clone()

if not (os.path.isdir("godot-cpp") and os.listdir("godot-cpp")):
    print_error(
        """godot-cpp is not available. Initialize the pinned submodule with:

    git submodule update --init --recursive"""
    )
    sys.exit(1)

env = SConscript("godot-cpp/SConstruct", {"env": env, "customs": customs})
if os.path.abspath(str(env.get("custom_api_file", ""))) != expected_api:
    print_error("godot-cpp did not retain MochaScript's pinned extension API")
    sys.exit(1)
if env.get("precision") != "single":
    print_error("godot-cpp precision must remain pinned to single")
    sys.exit(1)
if env.get("build_profile") or env.get("gdextension_dir"):
    print_error("godot-cpp profiles and alternate extension directories are not supported")
    sys.exit(1)

env.Append(CPPPATH=["src/"])
sources = (
    Glob("src/*.cpp")
    + Glob("src/integration/*.cpp")
    + Glob("src/language/*.cpp")
    + Glob("src/platform/*.cpp")
    + Glob("src/runtime/*.cpp")
    + Glob("src/testing/*.cpp")
)

suffix = env["suffix"].replace(".dev", "").replace(".universal", "")
lib_filename = "{}{}{}{}".format(
    env.subst("$SHLIBPREFIX"), libname, suffix, env.subst("$SHLIBSUFFIX")
)

library = env.SharedLibrary(
    "bin/{}/{}".format(env["platform"], lib_filename),
    source=sources,
)
copy = env.Install("{}/bin/{}/".format(projectdir, env["platform"]), library)

Default(library, copy)
