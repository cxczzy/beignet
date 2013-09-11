/*
 * Copyright © 2013 Intel Corporation
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 */

/*******************************************************************************
   This file is used to generating the gbe kernel binary.  These binary may be
   used in CL API, such as enqueue memory We generate the binary in build time
   to improve the performance.
 *******************************************************************************/
#include <fcntl.h>
#include <sys/mman.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <iostream>
#include <sstream>
#include <string>
#include <fstream>
#include <deque>
#include <vector>
#include <algorithm>
#include "backend/program.h"
#include "backend/program.hpp"

using namespace std;

#define FILE_NOT_FIND_ERR 1
#define FILE_MAP_ERR 2
#define FILE_BUILD_FAILED 3
#define FILE_SERIALIZATION_FAILED 4

class program_build_instance {

protected:
    string prog_path;
    string build_opt;
    static string bin_path;
    int fd;
    int file_len;
    const char* code;
    gbe::Program* gbe_prog;

public:
    program_build_instance (void) : fd(-1), file_len(0), code(NULL), gbe_prog(NULL) { }
    explicit program_build_instance (const char* file_path, const char* option = NULL)
        : prog_path(file_path), build_opt(option), fd(-1), file_len(0),
          code(NULL), gbe_prog(NULL) { }

    ~program_build_instance () {
        if (code) {
            munmap((void *)(code), file_len);
            code = NULL;
        }

        if (fd >= 0)
            close(fd);

        if (gbe_prog)
            gbe_program_delete(reinterpret_cast<gbe_program>(gbe_prog));
    }

    program_build_instance(program_build_instance&& other) = default;
#if 0
    {
#define SWAP(ELT) \
	do { \
	    auto elt = this->ELT; \
	    this->ELT = other.ELT; \
	    other.ELT = elt; \
	} while(0)

        SWAP(fd);
        SWAP(code);
        SWAP(file_len);
        SWAP(prog_path);
        SWAP(build_opt);
#undef SWAP
    }
#endif

    explicit program_build_instance(const program_build_instance& other) = delete;
    program_build_instance& operator= (const program_build_instance& other) {
        /* we do not want to be Lvalue copied, but operator is needed to instance the
           template of vector<program_build_instance>. */
        assert(1);
        return *this;
    }


    const char* file_map_open (void) throw (int);

    const char* get_code (void) {
        return code;
    }

    const string& get_program_path (void) {
        return prog_path;
    }

    int get_size (void) {
        return file_len;
    }

    void print_file (void) {
        cout << code << endl;
    }

    void dump (void) {
        cout << "program path: " << prog_path << endl;
        cout << "Build option: " << build_opt << endl;
        print_file();
    }

    static int set_bin_path (const char* path) {
        if (bin_path.size())
            return 0;

        bin_path = path;
        return 1;
    }

    void build_program(void) throw(int);
    void serialize_program(void) throw(int);
};

string program_build_instance::bin_path;

void program_build_instance::serialize_program(void) throw(int)
{
    ofstream ofs;
    ostringstream oss;
    ofs.open(bin_path, ofstream::out | ofstream::app | ofstream::binary);

    size_t sz = gbe_prog->serializeToBin(ofs);
    ofs.close();

    if (!sz) {
        throw FILE_SERIALIZATION_FAILED;
    }
}


void program_build_instance::build_program(void) throw(int)
{
    gbe_program opaque = gbe_program_new_from_source(code, 0, build_opt.c_str(), NULL, NULL);
    if (!opaque)
        throw FILE_BUILD_FAILED;

    gbe_prog = reinterpret_cast<gbe::Program*>(opaque);

    assert(gbe_program_get_kernel_num(opaque));
}

const char* program_build_instance::file_map_open(void) throw(int)
{
    void * address;

    /* Open the file */
    fd = ::open(prog_path.c_str(), O_RDONLY);
    if (fd < 0) {
        throw FILE_NOT_FIND_ERR;
    }

    /* Map it */
    file_len = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);
    address = mmap(0, file_len, PROT_READ, MAP_SHARED, fd, 0);
    if (address == NULL) {
        throw FILE_MAP_ERR;
    }

    code = reinterpret_cast<const char*>(address);
    return code;
}

typedef vector<program_build_instance> prog_vector;

int main (int argc, const char **argv)
{
    prog_vector prog_insts;
    vector<string> argv_saved;
    const char* build_opt;
    const char* file_path;
    int i;
    int oc;
    deque<int> used_index;

    if (argc < 2) {
        cout << "Usage: kernel_path [-pbuild_parameter]\n[-obin_path]" << endl;
        return 0;
    }

    used_index.assign(argc, 0);

    /* because getopt will re-sort the argv, so we save here. */
    for (i=0; i< argc; i++) {
        argv_saved.push_back(string(argv[i]));
    }

    while ( (oc = getopt(argc, (char * const *)argv, "o:p:")) != -1 ) {
        switch (oc) {
        case 'p':
        {
            int opt_index;

            if (argv[optind-1][0] == '-') {// -pXXX like
                opt_index = optind - 1;
            } else { // Must be -p XXXX mode
                opt_index = optind - 2;
                used_index[opt_index + 1] = 1;
            }

            /* opt must follow the file name.*/
            if ((opt_index < 2 ) || argv[opt_index-1][0] == '-') {
                cout << "Usage note: Building option must follow file name" << endl;
                return 1;
            }

            file_path = argv[opt_index - 1];
            build_opt = optarg;

            prog_insts.push_back(program_build_instance(file_path, build_opt));
            break;
        }

        case 'o':
            if (!program_build_instance::set_bin_path(optarg)) {
                cout << "Can not specify the bin path more than once." << endl;
                return 1;
            }
            used_index[optind-1] = 1;
            break;

        case ':':
            cout << "Miss the file option argument" << endl;
            return 1;

        default:
            cout << "Unknown opt" << endl;
        }
    }

    for (i=1; i < argc; i++) {
        //cout << argv_saved[i] << endl;
        if (argv_saved[i].size() && argv_saved[i][0] != '-') {
            if (used_index[i])
                continue;

            string file_name = argv_saved[i];
            prog_vector::iterator result = find_if(prog_insts.begin(), prog_insts.end(),
            [&](program_build_instance & prog_inst)-> bool {
                bool result = false;
                if (prog_inst.get_program_path() == file_name)
                    result = true;

                return result;
            });

            if (result == prog_insts.end()) {
                prog_insts.push_back(program_build_instance(file_name.c_str(), ""));
            }
        }
    }

    for (auto& inst : prog_insts) {
        try {
            inst.file_map_open();
            inst.build_program();
            inst.serialize_program();
        }
        catch (int & err_no) {
            if (err_no == FILE_NOT_FIND_ERR) {
                cout << "can not open the file " <<
                     inst.get_program_path() << endl;
            } else if (err_no == FILE_MAP_ERR) {
                cout << "map the file " <<
                     inst.get_program_path() << " failed" << endl;
            } else if (err_no == FILE_BUILD_FAILED) {
                cout << "build the file " <<
                     inst.get_program_path() << " failed" << endl;
            } else if (err_no == FILE_SERIALIZATION_FAILED) {
                cout << "Serialize the file " <<
                     inst.get_program_path() << " failed" << endl;
            }
            return -1;
        }
    }

    //for (auto& inst : prog_insts) {
    //    inst.dump();
    //}

    return 0;
}
