#!/usr/bin/env python

import os
import sys

from methods import print_error


libname = "mocha_script"
projectdir = "project"

local_env = Environment(tools=["default"], PLATFORM="")

customs = []
opts = Variables(customs, ARGUMENTS)
opts.Update(local_env)
local_env["custom_api_file"] = os.path.abspath("api/extension_api.json")
Help(opts.GenerateHelpText(local_env))

env = local_env.Clone()

if not (os.path.isdir("godot-cpp") and os.listdir("godot-cpp")):
    print_error(
        """godot-cpp is not available. Initialize the pinned submodule with:

    git submodule update --init --recursive"""
    )
    sys.exit(1)

env = SConscript("godot-cpp/SConstruct", {"env": env, "customs": customs})

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
