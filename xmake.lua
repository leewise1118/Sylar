add_rules("mode.debug", "mode.release")

target("test")
    set_kind("binary")
    add_links("yaml-cpp")
    add_files("tests/test_scheduler.cpp")
    add_files("sylar/*.cpp")


