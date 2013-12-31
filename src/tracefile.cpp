#include "tracefile.h"

#include "rand.h"

#include <vector>
#include <ctime>

using namespace devel::statprofiler;
using namespace std;

#define MAGIC   "=statprofiler"
#define VERSION 1

enum {
    SAMPLE_START = 1,
    SAMPLE_END   = 2,
    SUB_FRAME    = 3,
};

namespace {
    void append_hex(string &str, unsigned int value)
    {
        static const char digits[] = "0123456789abcdef";

        for (int i = 0; i < 8; ++i) {
            str += digits[value & 0xf];
            value >>= 4;
        }
    }

    size_t varint_size(int value)
    {
        return value < (1 << 7)  ? 1 :
               value < (1 << 14) ? 2 :
               value < (1 << 21) ? 3 :
                                   4;
    }

    size_t string_size(int length)
    {
        return varint_size(length) + length;
    }

    size_t string_size(const char *value)
    {
        return string_size(strlen(value));
    }

    int read_varint(FILE *in)
    {
        int res = 0;
        int v;

        for (;;) {
            v = getc(in);
            res = (res << 7) | (v & 0x7f);
            if (v & 0x80)
                break;
        }

        return res;
    }

    vector<char> read_bytes(FILE *in, size_t size)
    {
        vector<char> res(size);

        res.resize(size);
        fread(&res[0], 1, size, in);

        return res;
    }

    vector<char> read_string(FILE *in)
    {
        return read_bytes(in, read_varint(in));
    }

    void write_bytes(FILE *out, const char *bytes, size_t size)
    {
        fwrite(bytes, 1, size, out);
    }

    void write_byte(FILE *out, const char byte)
    {
        fwrite(&byte, 1, 1, out);
    }

    void write_varint(FILE *out, int value)
    {
        char buffer[4], *curr = &buffer[3];

        do {
            *curr-- = (value & 0x7f) | 0x80;
            value >>= 7;
        } while (value);

        buffer[3] &= 0x7f;
        write_bytes(out, curr + 1, (buffer + 4) - (curr + 1));
    }

    void write_string(FILE *out, const vector<char> &value)
    {
        write_varint(out, value.size());
        write_bytes(out, value.data(), value.size());
    }

    void write_string(FILE *out, const char *value, size_t length)
    {
        write_varint(out, length);
        write_bytes(out, value, length);
    }

    void write_string(FILE *out, const char *value)
    {
        write_string(out, value, strlen(value));
    }
}


TraceFileReader::TraceFileReader(const std::string &path)
{
    in = fopen(path.c_str(), "r");
}

TraceFileReader::~TraceFileReader()
{
    close();
}

void TraceFileReader::close()
{
    if (in)
        fclose(in);
    in = NULL;
}

SV *TraceFileReader::read_trace()
{
}


TraceFileWriter::TraceFileWriter(const string &path, bool is_template) :
    out(NULL), topmost_op_name(NULL)
{
    seed = rand_seed();
    open(path, is_template);
}

TraceFileWriter::~TraceFileWriter()
{
    close();
}

void TraceFileWriter::open(const std::string &path, bool is_template)
{
    close();
    output_file = path;

    if (is_template) {
        output_file += '.';
        append_hex(output_file, getpid());
        append_hex(output_file, time(NULL));
        for (int i = 0; i < 4; ++i) {
            rand(&seed);
            append_hex(output_file, seed);
        }
    }

    out = fopen(output_file.c_str(), "w");
    write_bytes(out, MAGIC, sizeof(MAGIC) - 1);
    write_varint(out, VERSION);
}

void TraceFileWriter::close()
{
    if (out) {
        string temp = output_file + "_";

        fclose(out);
        if (!rename(temp.c_str(), output_file.c_str()))
            unlink(temp.c_str());
    }

    out = NULL;
}

void TraceFileWriter::start_sample(unsigned int weight)
{
    write_byte(out, SAMPLE_START);
    write_varint(out, varint_size(weight) + string_size(topmost_op_name));
    write_varint(out, weight);
    write_string(out, topmost_op_name);
}

void TraceFileWriter::add_frame(unsigned int cxt_type, CV *sub, COP *line)
{
    write_byte(out, SUB_FRAME);
    const char *file = OutCopFILE(line);
    size_t file_size = strlen(file);
    int lineno = CopLINE(line);

    // require: cx->blk_eval.old_namesv
    // mPUSHs(newSVsv(cx->blk_eval.old_namesv));

    if (cxt_type != CXt_EVAL && cxt_type != CXt_NULL) {
        const char *package = "__ANON__", *name = "(unknown)";
        size_t package_size, name_size;

        // from Perl_pp_caller
	GV * const cvgv = CvGV(sub);
	if (cvgv && isGV(cvgv)) {
            GV *egv = GvEGVx(cvgv) ? GvEGVx(cvgv) : cvgv;
            HV *stash = GvSTASH(egv);

            if (stash)
                package = HvNAME(stash);
            name = GvNAME(egv);
	}

        package_size = strlen(package);
        name_size = strlen(name);

        write_varint(out, string_size(package_size) +
                     string_size(name_size) +
                     string_size(file_size) +
                     varint_size(lineno));
        write_string(out, package, package_size);
        write_string(out, name, name_size);
        write_string(out, file, file_size);
        write_varint(out, lineno);
    } else {
        write_varint(out, string_size(0) +
                     string_size(0) +
                     string_size(file_size) +
                     varint_size(lineno));
        write_string(out, "", 0);
        write_string(out, "", 0);
        write_string(out, file, file_size);
        write_varint(out, lineno);
    }
}

void TraceFileWriter::add_topmost_op(pTHX_ OP *o)
{
    topmost_op_name = o ? OP_NAME(o) : NULL;
}

void TraceFileWriter::end_sample()
{
    write_byte(out, SAMPLE_END);
}
