#ifndef _DEVEL_STATPROFILER_TRACEFILE
#define _DEVEL_STATPROFILER_TRACEFILE

#include "EXTERN.h"
#include "perl.h"

#include <string>
#include <vector>
#include <cstdio>


namespace devel {
    namespace statprofiler {
        class TraceFileReader
        {
        public:
            TraceFileReader(const std::string &path);
            ~TraceFileReader();

            bool is_valid() const { return in; }
            void close();

            SV *read_trace();
        private:
            std::FILE *in;
        };

        class TraceFileWriter
        {
        public:
            TraceFileWriter(const std::string &path, bool is_template);
            ~TraceFileWriter();

            void open(const std::string &path, bool is_template);
            void close();
            bool is_valid() const { return out; }

            void start_sample(unsigned int weight);
            void add_frame(unsigned int cxt_type, CV *sub, COP *line);
            void add_topmost_op(pTHX_ OP *o);
            void end_sample();

        private:
            std::FILE *out;
            std::string output_file;
            unsigned int seed;
            const char *topmost_op_name;
        };
    }
}

#endif
