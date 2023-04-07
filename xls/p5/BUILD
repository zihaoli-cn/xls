package(
    default_visibility = ["//xls:xls_internal"],
    licenses = ["notice"],  # Apache 2.0
)

cc_library(
  name = "keywords",
  srcs = ["keywords.cc"],
  hdrs = ["keywords.h"],
  deps = [
      "@com_google_absl//absl/strings:strings",
      "@com_google_absl//absl/status:status",
      "@com_google_absl//absl/status:statusor",
      ],
)


cc_library(
  name = "ast",
  srcs = ["ast.cc"],
  hdrs = ["ast.h"],
  deps = [
      ":keywords",
      "//xls/common/logging:logging",
      "//xls/common/status:status_macros",
      "//xls/p5/util:nlohmann_json",
      "@com_google_absl//absl/strings:strings",
      "@com_google_absl//absl/strings:str_format",
      "@com_google_absl//absl/status:status",
      "@com_google_absl//absl/types:optional",
      "@com_google_absl//absl/types:variant",
      "@com_google_absl//absl/container:flat_hash_set",
      ],
)

cc_library(
  name = "lowering_mapping",
  hdrs = ["lowering_mapping.h"],
  deps = [
      ":ast",
      "//xls/p5/util:nlohmann_json",
      "//xls/ir:ir",
  ]
)

cc_library(
  name = "json_ast_parser",
  srcs = ["json_ast_parser.cc"],
  hdrs = ["json_ast_parser.h"],
  deps = [
      ":keywords",
      ":ast",
      ":lowering_mapping",
      "//xls/p5/util:nlohmann_json",
      "//xls/common/logging:logging",
      "@com_google_absl//absl/strings:strings",
      "@com_google_absl//absl/status:statusor",
      "@com_google_absl//absl/status:status"
      ],
)


cc_library(
  name = "ir_converter",
  srcs = ["ir_converter.cc"],
  hdrs = ["ir_converter.h"],
  deps = [
      ":ast",
      ":lowering_mapping",
      "//xls/common/logging:logging",
      "//xls/ir:function_builder",
      "//xls/ir:ir",
      "//xls/ir:bits",
      "@boost//:icl",
      "@boost//:range",
      "@com_google_absl//absl/container:inlined_vector",
      "@com_google_absl//absl/strings:strings",
      "@com_google_absl//absl/strings:str_format",
      "@com_google_absl//absl/status:statusor",
      "@com_google_absl//absl/status:status",
      "@com_google_absl//absl/time",
      "@com_google_absl//absl/types:variant",
      ],
)

cc_library(
  name = "ast_mutation",
  srcs = ["ast_mutation.cc"],
  hdrs = ["ast_mutation.h"],
  deps = [
      ":ast",
      "//xls/common/logging:logging",
      "//xls/common/status:status_macros",
      "@com_google_absl//absl/status:status",
      "@com_google_absl//absl/types:optional",
      "@com_google_absl//absl/strings:str_format",
      ],
)

cc_library(
  name = "benchmark",
  srcs = ["benchmark.cc"],
  hdrs = ["benchmark.h"],
  deps = [
      ":ast",
      ":ir_converter",
      ":json_ast_parser",
      "//xls/common/file:filesystem",
      "//xls/common:visitor",
      "//xls/common/logging:logging",
      "//xls/common/status:status_macros",
      "//xls/delay_model:delay_estimator",
      "//xls/delay_model:delay_estimators",
      "//xls/delay_model:analyze_critical_path",
      "//xls/ir:ir",
      "//xls/p5/util:load_json",
      "//xls/p5/util:nlohmann_json",
      "@com_google_absl//absl/flags:flag",
      "@com_google_absl//absl/status:status",
      "@com_google_absl//absl/status:statusor",
      "@com_google_absl//absl/strings:str_format",
      "@com_google_absl//absl/strings",
      "@com_google_absl//absl/time",
      "@com_google_absl//absl/types:variant",
      ],
)