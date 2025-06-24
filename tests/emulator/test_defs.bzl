def cuttlefish_gfxstream_deqp_test(name, cuttlefish_create_args, deqp_include_patterns, cuttlefish_fetch_args = [], deqp_exclude_patterns = []):
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
        size = "large",
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
