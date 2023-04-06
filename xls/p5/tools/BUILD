package(
    default_visibility = ["//xls:xls_internal"],
    licenses = ["notice"],  # Apache 2.0
)
cc_binary(
    name = "benchmark_no_opt_main",
    srcs = ["benchmark_no_opt_main.cc"],
    deps = [
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/time",
        "//xls/codegen:module_signature",
        "//xls/codegen:pipeline_generator",
        "//xls/common:init_xls",
        "//xls/common:math_util",
        "//xls/common/file:filesystem",
        "//xls/common/logging",
        "//xls/common/status:status_macros",
        "//xls/delay_model:analyze_critical_path",
        "//xls/delay_model:delay_estimator",
        "//xls/delay_model:delay_estimators",
        "//xls/ir",
        "//xls/ir:ir_parser",
        "//xls/passes",
        "//xls/passes:standard_pipeline",
        "//xls/scheduling:pipeline_schedule",
    ],
)

cc_binary(
    name = "visualize_json_ast_main",
    srcs = ["visualize_json_ast_main.cc"],
    deps = [
        "//xls/p5/util:nlohmann_json",
        "//xls/p5/util:load_json",
        "//xls/p5:ast",
        "//xls/p5:json_ast_parser",
        "//xls/common:init_xls",
        "//xls/common/logging:log_flags",
        "//xls/common/logging:logging",
        "//xls/common/logging:vlog_is_on",
        "//xls/common/status:status_macros",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/status:status",
        "@com_google_absl//absl/strings",
    ],
)

cc_binary(
    name = "convert_ir_main",
    srcs = ["convert_ir_main.cc"],
    deps = [
        "//xls/p5/util:nlohmann_json",
        "//xls/p5/util:load_json",
        "//xls/p5:ast",
        "//xls/p5:json_ast_parser",
        "//xls/p5:ir_converter",
        "//xls/ir:ir",
        "//xls/common:init_xls",
        "//xls/common/logging:log_flags",
        "//xls/common/logging:logging",
        "//xls/common/logging:vlog_is_on",
        "//xls/common/status:status_macros",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/status:status",
        "@com_google_absl//absl/strings",
    ],
)

cc_binary(
    name = "json_ast_round_trip_main",
    srcs = ["json_ast_round_trip_main.cc"],
    deps = [
        "//xls/p5/util:nlohmann_json",
        "//xls/p5/util:load_json",
        "//xls/p5:ast",
        "//xls/p5:json_ast_parser",
        "//xls/common:init_xls",
        "//xls/common/logging:log_flags",
        "//xls/common/logging:logging",
        "//xls/common/logging:vlog_is_on",
        "//xls/common/status:status_macros",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/status:status",
        "@com_google_absl//absl/strings",
    ],
)

cc_binary(
    name = "ast_gen_if_main",
    srcs = ["ast_gen_main.cc"],
    deps = [
        "//xls/p5/util:nlohmann_json",
        "//xls/p5/util:load_json",
        "//xls/p5:ast",
        "//xls/p5:ast_mutation",
        "//xls/p5:json_ast_parser",
        "//xls/common:init_xls",
        "//xls/common/logging:log_flags",
        "//xls/common/logging:logging",
        "//xls/common/logging:vlog_is_on",
        "//xls/common/status:status_macros",
        "//xls/data_structures:union_find",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/status:status",
        "@com_google_absl//absl/strings",
    ],
)

cc_binary(
    name = "ast_gen_mod_main",
    srcs = ["ast_gen_mod_main.cc"],
    deps = [
        "//xls/p5/util:nlohmann_json",
        "//xls/p5/util:load_json",
        "//xls/p5:ast",
        "//xls/p5:ast_mutation",
        "//xls/p5:json_ast_parser",
        "//xls/common:init_xls",
        "//xls/common/logging:log_flags",
        "//xls/common/logging:logging",
        "//xls/common/logging:vlog_is_on",
        "//xls/common/status:status_macros",
        "//xls/data_structures:union_find",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/status:status",
        "@com_google_absl//absl/strings",
    ],
)

cc_binary(
    name = "hw_trans_benchmark_main",
    srcs = ["hw_trans_benchmark_main.cc"],
    deps = [
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "//xls/common:init_xls",
        "//xls/common/logging:log_flags",
        "//xls/common/logging:logging",
        "//xls/common/logging:vlog_is_on",
        "//xls/common/status:status_macros",
        "//xls/p5:benchmark",
        "//xls/p5/util:load_json",
    ],
)