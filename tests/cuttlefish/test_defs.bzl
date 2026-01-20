load("@rules_shell//shell:sh_test.bzl", "sh_test")

def _cuttlefish_gfxstream_xts_test(
        name,
        cuttlefish_create_args,
        cuttlefish_fetch_args,
        xts_args,
        xts_type,
        size):
    args = [
        "--cuttlefish-create-args=\"" + " ".join(cuttlefish_create_args) + "\"",
        "--cuttlefish-fetch-args=\"" + " ".join(cuttlefish_fetch_args) + "\"",
        "--gfxstream-library-path=$(location //host:gfxstream_backend)",
        "--xml-test-result-converter-path=$(location :convert_xts_xml_to_junit_xml.py)",
        "--xts-args=\"" + " ".join(xts_args) + "\"",
        "--xts-type=" + xts_type,
    ]

    data = [
        ":convert_xts_xml_to_junit_xml.py",
        "//host:gfxstream_backend",
    ]

    sh_test(
        name = name,
        size = size,
        srcs = [
            "run_cuttlefish_xts_tests.sh",
        ],
        args = args,
        data = data,
        tags = [
            "exclusive",
            "external",
            "no-sandbox",
        ],
    )

def cuttlefish_gfxstream_cts_test(
        name,
        cuttlefish_create_args,
        cts_include_patterns,
        cuttlefish_fetch_args = [],
        cts_exclude_patterns = [],
        size = "large"):
    cts_args = ["--include-filter=" + include_pattern for include_pattern in cts_include_patterns]
    if cts_exclude_patterns:
        cts_args.extend(["--exclude-filter=" + exclude_pattern for exclude_pattern in cts_exclude_patterns])

    _cuttlefish_gfxstream_xts_test(
        name,
        cuttlefish_create_args,
        cuttlefish_fetch_args,
        cts_args,
        "cts",
        size,
    )

def cuttlefish_gfxstream_deqp_test(
        name,
        cuttlefish_create_args,
        deqp_include_patterns,
        cuttlefish_fetch_args = [],
        deqp_exclude_patterns = [],
        size = "enormous"):
    cts_args = ["-m CtsDeqpTestCases "]
    cts_args.extend(["--module-arg CtsDeqpTestCases:include-filter:" + p for p in deqp_include_patterns])
    if deqp_exclude_patterns:
        cts_args.extend(["--module-arg CtsDeqpTestCases:exclude-filter:" + p for p in deqp_exclude_patterns])

    _cuttlefish_gfxstream_xts_test(
        name,
        cuttlefish_create_args,
        cuttlefish_fetch_args,
        cts_args,
        "cts",
        size,
    )

def cuttlefish_gfxstream_vts_test(
        name,
        cuttlefish_create_args,
        vts_include_patterns,
        cuttlefish_fetch_args = [],
        vts_exclude_patterns = [],
        size = "large"):
    vts_args = ["--include-filter=" + include_pattern for include_pattern in vts_include_patterns]
    if vts_exclude_patterns:
        vts_args.extend(["--exclude-filter=" + exclude_pattern for exclude_pattern in vts_exclude_patterns])

    _cuttlefish_gfxstream_xts_test(
        name,
        cuttlefish_create_args,
        cuttlefish_fetch_args,
        vts_args,
        "vts",
        size,
    )
