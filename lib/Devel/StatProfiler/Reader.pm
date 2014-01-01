package Devel::StatProfiler::Reader;

use strict;
use warnings;

require Devel::StatProfiler; # load XS but don't start profiling

package Devel::StatProfiler::StackFrame;

sub subroutine { $_[0]->{subroutine} }
sub file { $_[0]->{file} }
sub line { $_[0]->{line} }

package Devel::StatProfiler::StackTrace;

sub weight { $_[0]->{weight} }
sub op_name { $_[0]->{op_name} }
sub frames { $_[0]->{frames} }

1;
