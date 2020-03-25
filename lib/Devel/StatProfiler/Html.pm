package Devel::StatProfiler::Html;

use strict;
use warnings;

use Devel::StatProfiler::Report;

sub process {
    my (%opts) = @_;
    my $report = Devel::StatProfiler::Report->new(
        flamegraph      => 1,
        sources         => 1,
        mixed_process   => 1,
    );

    for my $f (@{$opts{files}}) {
        my $r = Devel::StatProfiler::Reader->new($f);
        my ($process_id) = @{$r->get_genealogy_info};
        $report->add_trace_file($r);
        $report->map_source($process_id);
    }

    return $report;
}

sub process_and_output {
    my (%opts) = @_;
    my $report = process(%opts);
    my $diagnostics = $report->output($opts{output});

    for my $diagnostic (@$diagnostics) {
        print STDERR $diagnostic, "\n";
    }
}

1;
