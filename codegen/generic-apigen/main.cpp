/*
 * Copyright (C) 2011 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdio.h>
#include <stdlib.h>

#include <string>

#include "api_gen.h"
#include "entry_point.h"
#include "errors.h"
#include "type_factory.h"

const std::string SPEC_EXTENSION = std::string(".in");
const std::string ATTRIB_EXTENSION = std::string(".attrib");
const std::string TYPES_EXTENTION = std::string(".types");

void usage(const char* filename) {
    fprintf(stderr, "Usage: %s [options] <base name>\n", filename);
    fprintf(stderr, "\t-h: This message\n");
    fprintf(stderr, "\t-E <dir>: generate encoder into dir\n");
    fprintf(stderr, "\t-D <dir>: generate decoder into dir\n");
    fprintf(stderr, "\t-i: input dir, local directory by default\n");
    fprintf(stderr,
            "\t-T : generate attribute template into the input directory\n\t\tno other files are "
            "generated\n");
    fprintf(stderr, "\t-W : generate wrapper into dir\n");
}

int main(int argc, char* argv[]) {
    std::string baseName;
    std::string encoderDir;
    std::string decoderDir;
    std::string wrapperDir;
    std::string inDir = ".";

    bool generateAttributesTemplate = false;

    std::vector<std::string> args;
    for (int i = 1; i < argc; i++) {
        args.push_back(std::string(argv[i]));
    }

    while (!args.empty()) {
        std::string arg = args[0];
        args.erase(args.begin());

        if (arg == "-h") {
            usage(argv[0]);
            return 0;
        }

        if (arg == "-T") {
            generateAttributesTemplate = true;
            continue;
        }

        if (args.empty()) {
            fprintf(stderr, "Missing argument value: %s\n", arg.c_str());
            return BAD_USAGE;
        }
        std::string argopt = args[0];
        args.erase(args.begin());

        if (arg == "-B") {
            baseName = argopt;
        } else if (arg == "-W") {
            wrapperDir = argopt;
        } else if (arg == "-E") {
            encoderDir = argopt;
        } else if (arg == "-D") {
            decoderDir = argopt;
        } else if (arg == "-i") {
            inDir = argopt;
        } else {
            fprintf(stderr, "Unknown argument: %s\n", arg.c_str());
            return BAD_USAGE;
        }
    }

    if (encoderDir.size() == 0 && decoderDir.size() == 0 && generateAttributesTemplate == false &&
        wrapperDir.size() == 0) {
        fprintf(stderr, "No output specified - aborting\n");
        return BAD_USAGE;
    }

    ApiGen apiEntries(baseName);

    // init types;
    std::string typesFilename = inDir + "/" + baseName + TYPES_EXTENTION;

    if (TypeFactory::instance()->initFromFile(typesFilename) < 0) {
        fprintf(stderr, "missing or error reading types file: %s...ignored\n",
                typesFilename.c_str());
    }

    std::string filename = inDir + "/" + baseName + SPEC_EXTENSION;
    if (apiEntries.readSpec(filename) < 0) {
        perror(filename.c_str());
        return BAD_SPEC_FILE;
    }

    if (generateAttributesTemplate) {
        apiEntries.genAttributesTemplate(inDir + "/" + baseName + ATTRIB_EXTENSION);
        exit(0);
    }

    std::string attribFileName = inDir + "/" + baseName + ATTRIB_EXTENSION;
    if (apiEntries.readAttributes(attribFileName) < 0) {
        perror(attribFileName.c_str());
        fprintf(stderr, "failed to parse attributes\n");
        exit(1);
    }

    if (encoderDir.size() != 0) {
        apiEntries.genOpcodes(encoderDir + "/" + baseName + "_opcodes.h");
        apiEntries.genContext(encoderDir + "/" + baseName + "_client_context.h",
                              ApiGen::CLIENT_SIDE);
        apiEntries.genContextImpl(encoderDir + "/" + baseName + "_client_context.cpp",
                                  ApiGen::CLIENT_SIDE);

        apiEntries.genProcTypes(encoderDir + "/" + baseName + "_client_proc.h",
                                ApiGen::CLIENT_SIDE);
        apiEntries.genFuncTable(encoderDir + "/" + baseName + "_ftable.h", ApiGen::CLIENT_SIDE);

        apiEntries.genEntryPoints(encoderDir + "/" + baseName + "_entry.cpp", ApiGen::CLIENT_SIDE);
        apiEntries.genEncoderHeader(encoderDir + "/" + baseName + "_enc.h");
        apiEntries.genEncoderImpl(encoderDir + "/" + baseName + "_enc.cpp");
    }

    if (decoderDir.size() != 0) {
        apiEntries.genOpcodes(decoderDir + "/" + baseName + "_opcodes.h");
        apiEntries.genProcTypes(decoderDir + "/" + baseName + "_server_proc.h",
                                ApiGen::SERVER_SIDE);
        apiEntries.genContext(decoderDir + "/" + baseName + "_server_context.h",
                              ApiGen::SERVER_SIDE);
        apiEntries.genContextImpl(decoderDir + "/" + baseName + "_server_context.cpp",
                                  ApiGen::SERVER_SIDE);
        apiEntries.genDecoderHeader(decoderDir + "/" + baseName + "_dec.h");
        apiEntries.genDecoderImpl(decoderDir + "/" + baseName + "_dec.cpp");
    }

    if (wrapperDir.size() != 0) {
        apiEntries.genProcTypes(wrapperDir + "/" + baseName + "_wrapper_proc.h",
                                ApiGen::WRAPPER_SIDE);
        apiEntries.genContext(wrapperDir + "/" + baseName + "_wrapper_context.h",
                              ApiGen::WRAPPER_SIDE);
        apiEntries.genContextImpl(wrapperDir + "/" + baseName + "_wrapper_context.cpp",
                                  ApiGen::WRAPPER_SIDE);
        apiEntries.genEntryPoints(wrapperDir + "/" + baseName + "_wrapper_entry.cpp",
                                  ApiGen::WRAPPER_SIDE);
    }

#ifdef DEBUG_DUMP
    int withPointers = 0;
    printf("%d functions found\n", int(apiEntries.size()));
    for (int i = 0; i < apiEntries.size(); i++) {
        if (apiEntries[i].hasPointers()) {
            withPointers++;
            apiEntries[i].print();
        }
    }
    fprintf(stdout, "%d entries has poitners\n", withPointers);
#endif
}
