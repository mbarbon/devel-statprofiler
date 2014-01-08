#include "tracefile.h"

#include "rand.h"

#include <ctime>

using namespace devel::statprofiler;
using namespace std;

#define MAGIC   "=statprofiler"
#define VERSION 1

#if PERL_SUBVERSION < 16
# ifndef GvNAMEUTF8
#   define GvNAMEUTF8(foo) 0
# endif
#endif

enum {
    TAG_SAMPLE_START            = 1,
    TAG_SAMPLE_END              = 2,
    TAG_SUB_FRAME               = 3,
    TAG_EVAL_FRAME              = 4, // TODO implement
    TAG_SECTION_START           = 198, // TODO implement
    TAG_SECTION_END             = 199, // TODO implement
    TAG_CUSTOM_META             = 200, // TODO implement
    TAG_META_PERL_VERSION       = 201, // TODO implement
    TAG_META_TICK_DURATION      = 202, // TODO implement
    TAG_META_STACK_SAMPLE_DEPTH = 203, // TODO implement
    TAG_META_LIBRARY_VERSION    = 204, // TODO implement
    TAG_HEADER_SEPARATOR        = 254,
    TAG_TAG_CONTINUATION        = 255 // just reserved for now
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
        return string_size(value ? strlen(value) : 0);
    }

    void skip_bytes(FILE *in, size_t size)
    {
        char buffer[128];

        for (int read = -1; read && size; ) {
            size_t to_read = min(sizeof(buffer), size);

            read = fread(buffer, 1, to_read, in);
            size -= read;

            if (to_read != read)
                croak("Unexpected end-of-file while skipping over data");
        }
    }

    int read_varint(FILE *in)
    {
        int res = 0;
        int v;

        for (;;) {
            v = fgetc(in);
            if (v == EOF)
                croak("Unexpected end-of-file while reading a varint");
            res = (res << 7) | (v & 0x7f);
            if (!(v & 0x80))
                break;
        }

        return res;
    }

    SV *read_string(pTHX_ FILE *in)
    {
        int flags = fgetc(in);
        int size = read_varint(in);

        if (flags == EOF)
            croak("Unexpected end-of-file while reading a string");

        // don't return undef when length is 0
        if (size == 0)
            return sv_2mortal(newSVpvn("", 0));

        SV *sv = sv_2mortal(newSV(size));

        SvPOK_on(sv);
        SvCUR_set(sv, size);
        if (flags & 1)
            SvUTF8_on(sv);

        if (fread(SvPVX(sv), 1, size, in) != size)
            croak("Unexpected end-of-file while reading a string");

        return sv;
    }

    int write_bytes(FILE *out, const char *bytes, size_t size)
    {
        return fwrite(bytes, 1, size, out) != 0;
    }

    int write_byte(FILE *out, const char byte)
    {
        return fwrite(&byte, 1, 1, out) != 0;
    }

    int write_varint(FILE *out, int value)
    {
        char buffer[10], *curr = &buffer[sizeof(buffer) - 1];

        do {
            *curr-- = (value & 0x7f) | 0x80;
            value >>= 7;
        } while (value);

        buffer[sizeof(buffer) - 1] &= 0x7f;
        return write_bytes(out, curr + 1, (buffer + sizeof(buffer)) - (curr + 1));
    }

    int write_string(FILE *out, const char *value, size_t length, bool utf8)
    {
        int status = 0;
        status += write_byte(out, utf8 ? 1 : 0);
        status += write_varint(out, length);
        status += write_bytes(out, value, length);
        return status;
    }

    int write_string(FILE *out, const char *value, bool utf8)
    {
        return write_string(out, value, value ? strlen(value) : 0, utf8);
    }
}


TraceFileReader::TraceFileReader(pTHX)
  : in(NULL), file_version(0)
{
    SET_THX_MEMBER
}

TraceFileReader::~TraceFileReader()
{
    close();
}

void TraceFileReader::open(const std::string &path)
{
    close();
    in = fopen(path.c_str(), "r");
    read_header();
}

void TraceFileReader::read_header()
{
    char magic[sizeof(MAGIC) - 1];

    if (fread(magic, 1, sizeof(magic), in) != sizeof(magic))
        croak("Unexpected end-of-file while reading file magic");
    if (strncmp(magic, MAGIC, sizeof(magic)))
        croak("Invalid file magic");

    // In future, will check that the version is at least not newer
    // than this library's file format version. That's necessary even
    // if there's a backcompat layer.
    int version_from_file = read_varint(in);
    if (version_from_file < 1 || version_from_file > VERSION)
        croak("Incompatible file format version %i", version_from_file);

    file_version = (unsigned int)version_from_file;

    // TODO this becomes a loop reading header records
    bool cont = 1;
    while (cont) {
        const int tag = fgetc(in);

        switch (tag) {
        case EOF:
            croak("Invalid input file: File ends before end of file header");
        case TAG_HEADER_SEPARATOR:
            cont = 0;
            break;

        // TODO use the actual header data!
        case TAG_META_PERL_VERSION: {
            const int perl_revision   = read_varint(in);
            const int perl_version    = read_varint(in);
            const int perl_subversion = read_varint(in);
            break;
        }
        case TAG_META_TICK_DURATION: {
            const int tick_duration = read_varint(in);
            break;
        }
        case TAG_META_STACK_SAMPLE_DEPTH: {
            const int stack_sample_depth = read_varint(in);
            break;
        }

        default:
            croak("Invalid input file: Invalid header record tag (%i)", tag);
        }
    }
}

void TraceFileReader::close()
{
    if (in)
        fclose(in);
    in = NULL;
}

SV *TraceFileReader::read_trace()
{
    // This could possibly be cached across read_trace calls and may
    // be worthwhile if there's lots.
    HV *st_stash = gv_stashpv("Devel::StatProfiler::StackTrace", 0);
    HV *sf_stash = gv_stashpv("Devel::StatProfiler::StackFrame", 0);
    HV *sample;
    AV *frames;

    for (;;) {
        int type = fgetc(in);

        if (type == EOF)
            return newSV(0);

        int size = read_varint(in);

        switch (type) {
        case TAG_SAMPLE_START: {
            int weight = read_varint(in);
            SV *op_name = read_string(aTHX_ in);

            sample = (HV *) sv_2mortal((SV *) newHV());
            frames = newAV();

            hv_store(sample, "frames", 6, newRV_noinc((SV *) frames), 0);
            hv_store(sample, "weight", 6, newSViv(weight), 0);
            hv_store(sample, "op_name", 7, SvREFCNT_inc(op_name), 0);
            break;
        }
        default:
            skip_bytes(in, size);
            break;
        case TAG_SUB_FRAME: {
            SV *package = read_string(aTHX_ in);
            SV *name = read_string(aTHX_ in);
            SV *file = read_string(aTHX_ in);
            int line = read_varint(in);
            HV *frame = newHV();

            if (SvCUR(package) || SvCUR(name)) {
                SV *fullname = newSV(SvCUR(package) + 2 + SvCUR(name));

                SvPOK_on(fullname);
                sv_catsv(fullname, package);
                sv_catpvn(fullname, "::", 2);
                sv_catsv(fullname, name);

                hv_store(frame, "subroutine", 10, fullname, 0);
            }
            else
                hv_store(frame, "subroutine", 10, newSVpvn("", 0), 0);

            hv_store(frame, "file", 4, SvREFCNT_inc(file), 0);
            hv_store(frame, "line", 4, newSViv(line), 0);
            av_push(frames, sv_bless(newRV_noinc((SV *) frame), sf_stash));

            break;
        }
        case TAG_SAMPLE_END:
            skip_bytes(in, size);
            return sv_bless(newRV_inc((SV *) sample), st_stash);
        }
    }
}


TraceFileWriter::TraceFileWriter(pTHX_ const string &path, bool is_template) :
    out(NULL)
{
    SET_THX_MEMBER
    seed = rand_seed();
    open(path, is_template);
}

TraceFileWriter::~TraceFileWriter()
{
    close();
}

int TraceFileWriter::open(const std::string &path, bool is_template)
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
    if (!out)
        return 1;

    return 0;
}

int TraceFileWriter::write_perl_version()
{
    int status = 0;
    status += write_byte(out, TAG_META_PERL_VERSION);
    status += write_varint(out, PERL_REVISION);
    status += write_varint(out, PERL_VERSION);
    status += write_varint(out, PERL_SUBVERSION);
    return status;
}

int TraceFileWriter::write_header(unsigned int sampling_interval,
                                  unsigned int stack_collect_depth)
{
    int status = 0;
    status += write_bytes(out, MAGIC, sizeof(MAGIC) - 1);
    status += write_varint(out, VERSION);

    // Write meta data: Perl version, tick duration, stack sample depth
    status += write_perl_version();

    status += write_byte(out, TAG_META_TICK_DURATION);
    status += write_varint(out, sampling_interval);

    status += write_byte(out, TAG_META_STACK_SAMPLE_DEPTH);
    status += write_varint(out, stack_collect_depth);

    status += write_byte(out, TAG_HEADER_SEPARATOR);
    return status;
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

int TraceFileWriter::start_sample(unsigned int weight, OP *current_op)
{
    const char *op_name = current_op ? OP_NAME(current_op) : NULL;
    int status = 0;

    status += write_byte(out, TAG_SAMPLE_START);
    status += write_varint(out, varint_size(weight) + string_size(op_name));
    status += write_varint(out, weight);
    status += write_string(out, op_name, false);

    return status;
}

int TraceFileWriter::add_frame(unsigned int cxt_type, CV *sub, COP *line)
{
    const char *file = OutCopFILE(line);
    size_t file_size = strlen(file);
    int lineno = CopLINE(line);
    int status = 0;

    status += write_byte(out, TAG_SUB_FRAME);

    // require: cx->blk_eval.old_namesv
    // mPUSHs(newSVsv(cx->blk_eval.old_namesv));

    if (cxt_type != CXt_EVAL && cxt_type != CXt_NULL) {
        const char *package = "__ANON__", *name = "(unknown)";
        bool package_utf8 = false, name_utf8 = false;
        size_t package_size = 8, name_size = 9;

        // from Perl_pp_caller
	GV * const cvgv = CvGV(sub);
	if (cvgv && isGV(cvgv)) {
            GV *egv = GvEGVx(cvgv) ? GvEGVx(cvgv) : cvgv;
            HV *stash = GvSTASH(egv);

            if (stash) {
                package = HvNAME(stash);
#if PERL_SUBVERSION >= 16
                package_utf8 = HvNAMEUTF8(stash);
                package_size = HvNAMELEN(stash);
#else
                package_utf8 = 0;
                package_size = strlen(package);
#endif
            }
            name = GvNAME(egv);
            name_utf8 = GvNAMEUTF8(egv);
            name_size = GvNAMELEN(egv);
	}

        status += write_varint(out, string_size(package_size) +
                                    string_size(name_size) +
                                    string_size(file_size) +
                                    varint_size(lineno));
        status += write_string(out, package, package_size, package_utf8);
        status += write_string(out, name, name_size, name_utf8);
        status += write_string(out, file, file_size, false);
        status += write_varint(out, lineno);
    } else {
        status += write_varint(out, string_size(0) +
                                    string_size(0) +
                                    string_size(file_size) +
                                    varint_size(lineno));
        status += write_string(out, "", 0, false);
        status += write_string(out, "", 0, false);
        status += write_string(out, file, file_size, false);
        status += write_varint(out, lineno);
    }

    return status;
}

int TraceFileWriter::end_sample()
{
    int status = 0;
    status += write_byte(out, TAG_SAMPLE_END);
    status += write_varint(out, 0);
    return status;
}
