set_languages("c++20")

add_rules("mode.release", "mode.debug")

add_requires("boost >=1.60", { system = true, configs = { system = true, thread = true, atomic = true } })
add_requires("libevent", "libuv", "libcurl")

for _, filepath in ipairs(os.filedirs("test*")) do
	target(filepath)
		set_kind("binary")
		add_files(filepath .. "/*.cpp")

		add_deps("ucoro")
		add_cxxflags("-foptimize-sibling-calls", {tools = { "gcc", "gxx" }})

		add_tests("run")
		set_default(false)
end

target("testqt")
	add_rules("qt.console")

target("testlibevent")
	add_packages("libevent")

target("testlibuv")
	add_packages("libuv")

target("test_curl")
	add_packages("libcurl")
