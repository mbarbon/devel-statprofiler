## Devel::StatProfiler
A low-overhead statistical Perl profiler for production use

## Simple usage

```sh
# profile (needs multiple runs with representative data!)
perl -MDevel::StatProfiler myscript.pl input1.txt
perl -MDevel::StatProfiler myscript.pl input2.txt
perl -MDevel::StatProfiler myscript.pl input3.txt
perl -MDevel::StatProfiler myscript.pl input1.txt
  
# prepare a report from profile data
statprofilehtml
```

[Plack::Middleware::StatProfiler](https://github.com/mbarbon/plack-middleware-statprofiler)
provides Plack integration for the profiler.

## Description

Devel::StatProfiler was created to gather profiling data from a production
system with as little overhead as possible: synthetic benchmarks in the
[benchmarks](https://github.com/mbarbon/devel-statprofiler/tree/master/benchmarks) directory
are written as contrived worst cases, and measure a slowdown up to 5%, while actual production
usage on a high-traffic website measured a 1-2% slowdown.

Profiling data is saved on disk while it is being collected, and can be processed at any moment
(there is no need to wait for the profiled process to terminate).

## How it works

The profiler gathers a stack trace from the Perl interpreter at fixed time intervals. Every time
a function/line of code appears in a stack trace, it counts as one sample: as the number of collected
samples increases, their distribution approximates the distribution of how much time is spent on each line
by the Perl interpreter.

The instrumentation hooks the Perl interpreter runloop, thus avoiding the overhead of the Perl debugger
API used by other profilers.

The downside of statistical profiling is that it can't sollect some interesting data available to other
profilers, for example the number of calls to a subroutine, or the exact amount of time spent on
a subroutine/line. The upside is that this level of detail is generally not needed when looking for
bottlenecks on a production system.
