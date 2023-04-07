package(
    default_visibility = ["//xls:xls_internal"],
    licenses = ["notice"],  # Apache 2.0
)

cc_library(
  name = "nlohmann_json",
  hdrs = ["json.hpp"],
)

cc_library(
  name = "load_json",
  srcs = ["load_json.cc"],
  hdrs = ["load_json.h"],
  deps = [
      ":nlohmann_json",
      "//xls/common/logging:logging",
      "//xls/p5:keywords",
      "@com_google_absl//absl/strings:strings",
      "@com_google_absl//absl/status:status",
      "@com_google_absl//absl/status:statusor",
      "@com_google_absl//absl/container:flat_hash_set"
      ],
)