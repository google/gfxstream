def cuttlefish_gfxstream_cts_test(name,
                                  cuttlefish_create_args,
                                  cts_include_patterns,
                                  cuttlefish_fetch_args = [],
                                  cts_exclude_patterns = [],
                                  size = "large"):
    cts_args = ["--include-filter=" + include_pattern for include_pattern in cts_include_patterns]
    if cts_exclude_patterns:
        cts_args.extend(["--exclude-filter=" + exclude_pattern for exclude_pattern in cts_exclude_patterns])

    args = [
        "--cts-args=\"" + "".join(cts_args) + "\"",
        "--cuttlefish-create-args=\"" + " ".join(cuttlefish_create_args) + "\"",
        "--cuttlefish-fetch-args=\"" + " ".join(cuttlefish_fetch_args) + "\"",
        "--gfxstream-library-path=$(location //host:gfxstream_backend)",
        "--xml-test-result-converter-path=$(location :convert_cts_xml_to_junit_xml.py)",
    ]

    data = [
        ":convert_cts_xml_to_junit_xml.py",
        "//host:gfxstream_backend",
    ]

    native.sh_test(
        name = name,
        size = size,
        srcs = [
            "run_cuttlefish_cts_tests.sh",
        ],
        args = args,
        data = data,
        tags = [
            "exclusive",
            "external",
            "no-sandbox",
        ],
    )

def cuttlefish_gfxstream_deqp_test(name,
                                   cuttlefish_create_args,
                                   deqp_include_patterns,
                                   cuttlefish_fetch_args = [],
                                   deqp_exclude_patterns = [],
                                   size = "enormous"):
    cts_arg_string = "-m CtsDeqpTestCases "
    cts_arg_string += " ".join(["--module-arg CtsDeqpTestCases:include-filter:" + p for p in deqp_include_patterns])

    args = [
        "--cts-args=\"" + cts_arg_string + "\"",
        "--cuttlefish-create-args=\"" + " ".join(cuttlefish_create_args) + "\"",
        "--cuttlefish-fetch-args=\"" + " ".join(cuttlefish_fetch_args) + "\"",
        "--gfxstream-library-path=$(location //host:gfxstream_backend)",
        "--xml-test-result-converter-path=$(location :convert_cts_xml_to_junit_xml.py)",
    ]

    data = [
        ":convert_cts_xml_to_junit_xml.py",
        "//host:gfxstream_backend",
    ]

    native.sh_test(
        name = name,
        size = size,
        srcs = [
            "run_cuttlefish_cts_tests.sh",
        ],
        args = args,
        data = data,
        tags = [
            "exclusive",
            "external",
            "no-sandbox",
        ],
    )
