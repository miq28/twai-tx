Import("env")
import os

ccache = "ccache"

env["CC"]  = f"{ccache} {env['CC']}"
env["CXX"] = f"{ccache} {env['CXX']}"
# env["LINK"] = env["CXX"]