#!/usr/bin/env perl

use t::lib::Test ':spawn';

use Devel::StatProfiler::Html;

my ($profile_dir, $template);
BEGIN { ($profile_dir, $template) = temp_profile_dir(); }

use Devel::StatProfiler -template => $template, -interval => 1000;

sub before { take_sample() }
sub after  { take_sample() }

Devel::StatProfiler::start_section('action');

before();

spawn(sub {
    take_sample();
})->join;

after();

Devel::StatProfiler::end_section('action');
Devel::StatProfiler::stop_profile();

my @files = glob "$template.*";
my $r = Devel::StatProfiler::Html::process(files => \@files);
my $a = $r->{aggregate};

my @traces = map {
    s{__FILE__}{__FILE__}reg =~
    s{t/lib/Test\.pm}{$TEST_PM}rg =~
    s{\$SPAWN_LINE}{$t::lib::Test::SPAWN_LINE}rg =~
    s{\$TAKE_SAMPLE_LINE}{$t::lib::Test::TAKE_SAMPLE_LINE}rg
}
  ($] > 5.038 ? '__FILE__:main;t/lib/Test.pm:t::lib::Test::spawn:$SPAWN_LINE' : 't/lib/Test.pm:main' ) . ';__FILE__:main::__ANON__:20;t/lib/Test.pm:t::lib::Test::take_sample:$TAKE_SAMPLE_LINE;(unknown):Time::HiRes::usleep',
  qw(
    __FILE__:main;__FILE__:main::before:12;t/lib/Test.pm:t::lib::Test::take_sample:$TAKE_SAMPLE_LINE;(unknown):Time::HiRes::usleep
    __FILE__:main;__FILE__:main::after:13;t/lib/Test.pm:t::lib::Test::take_sample:$TAKE_SAMPLE_LINE;(unknown):Time::HiRes::usleep
);

my $ok = 1;
for my $trace (@traces) {
    $ok &&= ok(exists $a->{flames}{$trace}, "present - $trace");
}

if (!$ok) {
    diag("Test failed, dumping flame graph");
    diag($_) for sort keys %{$a->{flames}};
}

done_testing();
