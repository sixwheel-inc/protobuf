load(
    "//bazel:upb_proto_library.bzl",
    "upb_c_proto_library",
    "upb_proto_reflection_library",
)

# begin:google_only
# package(default_applicable_licenses = ["//upb:license"])
# end:google_only

licenses(["notice"])

# Def to Proto

cc_library(
    name = "def_to_proto",
    srcs = ["def_to_proto.c"],
    hdrs = ["def_to_proto.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//upb:port",
        "//upb:reflection",
        "//upb:reflection_internal",
    ],
)

proto_library(
    name = "def_to_proto_test_proto",
    srcs = [
        "def_to_proto_public_import_test.proto",
        "def_to_proto_regular_import_test.proto",
        "def_to_proto_test.proto",
    ],
)

upb_c_proto_library(
    name = "def_to_proto_test_upb_proto",
    deps = ["def_to_proto_test_proto"],
)

upb_proto_reflection_library(
    name = "def_to_proto_test_upb_proto_reflection",
    deps = ["def_to_proto_test_proto"],
)

cc_library(
    name = "def_to_proto_test_lib",
    testonly = 1,
    hdrs = ["def_to_proto_test.h"],
    deps = [
        ":def_to_proto",
        "//:protobuf",
        "//upb:base",
        "//upb:descriptor_upb_proto",
        "//upb:mem",
        "//upb:reflection_internal",
        "@com_google_googletest//:gtest",
    ],
)

cc_test(
    name = "def_to_proto_test",
    srcs = ["def_to_proto_test.cc"],
    deps = [
        ":def_to_proto",
        ":def_to_proto_test_lib",
        ":def_to_proto_test_upb_proto",
        ":def_to_proto_test_upb_proto_reflection",
        "//:protobuf",
        "@com_google_googletest//:gtest_main",
        "//upb:descriptor_upb_proto_reflection",
        "//upb:mem",
        "//upb:reflection",
        "//upb/test:parse_text_proto",
        "@com_google_absl//absl/strings",
    ],
)

# begin:google_only
# cc_test(
#     name = "def_to_proto_fuzz_test",
#     srcs = ["def_to_proto_fuzz_test.cc"],
#     tags = ["clang_only"],
#     deps = [
#         ":def_to_proto_test_lib",
#         "@com_google_googletest//:gtest_main",
#         "//testing/fuzzing:fuzztest",
#     ],
# )
# end:google_only

# Required fields

cc_library(
    name = "required_fields",
    srcs = ["required_fields.c"],
    hdrs = ["required_fields.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//upb:base",
        "//upb:mem",
        "//upb:message",
        "//upb:port",
        "//upb:reflection",
    ],
)

proto_library(
    name = "required_fields_test_proto",
    srcs = [
        "required_fields_editions_test.proto",
        "required_fields_test.proto",
    ],
)

upb_c_proto_library(
    name = "required_fields_test_upb_proto",
    deps = ["required_fields_test_proto"],
)

upb_proto_reflection_library(
    name = "required_fields_test_upb_proto_reflection",
    deps = ["required_fields_test_proto"],
)

cc_test(
    name = "required_fields_test",
    srcs = ["required_fields_test.cc"],
    deps = [
        ":required_fields",
        ":required_fields_test_upb_proto",
        ":required_fields_test_upb_proto_reflection",
        "@com_google_googletest//:gtest_main",
        "//upb:base",
        "//upb:json",
        "//upb:mem",
        "//upb:reflection",
        "//upb:reflection_internal",
        "@com_google_absl//absl/strings",
    ],
)

# Compare

cc_library(
    name = "compare",
    srcs = ["compare.c"],
    hdrs = ["compare.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//upb:base",
        "//upb:eps_copy_input_stream",
        "//upb:mem",
        "//upb:port",
        "//upb:wire_reader",
        "//upb:wire_types",
    ],
)

cc_test(
    name = "compare_test",
    srcs = ["compare_test.cc"],
    deps = [
        ":compare",
        "@com_google_googletest//:gtest_main",
        "//upb:wire_internal",
        "//upb:wire_types",
        "@com_google_absl//absl/strings",
    ],
)

# begin:github_only
filegroup(
    name = "source_files",
    srcs = [
        "compare.c",
        "compare.h",
        "def_to_proto.c",
        "def_to_proto.h",
        "required_fields.c",
        "required_fields.h",
    ],
    visibility = ["//python/dist:__pkg__"],
)
# end:github_only
