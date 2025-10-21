#!/usr/bin/env perl

use t::lib::Test ':spawn';

use Devel::StatProfiler::Html;

my ($profile_dir, $template);
BEGIN { ($profile_dir, $template) = temp_profile_dir(); }

use Devel::StatProfiler -template => $template, -interval => 1000;

sub before { take_sample() }
sub after  { take_sample() }

before();

spawn(sub {
    take_sample();
})->join;

after();

Devel::StatProfiler::stop_profile();

my @files = glob "$template.*";
my $r = Devel::StatProfiler::Html::process(files => \@files);
my $a = $r->{aggregate};

my @traces = map {
    s{__FILE__}{__FILE__}reg =~
    s{\$SPAWN_LINE}{$t::lib::Test::SPAWN_LINE}rg =~
    s{\$TAKE_SAMPLE_LINE}{$t::lib::Test::TAKE_SAMPLE_LINE}rg
}
  ($] > 5.038 ? '__FILE__:main;t/lib/Test.pm:t::lib::Test::spawn:$SPAWN_LINE' : 't/lib/Test.pm:main' ) . ';__FILE__:main::__ANON__:18;t/lib/Test.pm:t::lib::Test::take_sample:$TAKE_SAMPLE_LINE;(unknown):Time::HiRes::usleep',
  qw(
    __FILE__:main;__FILE__:main::before:12;t/lib/Test.pm:t::lib::Test::take_sample:$TAKE_SAMPLE_LINE;(unknown):Time::HiRes::usleep
    __FILE__:main;__FILE__:main::after:13;t/lib/Test.pm:t::lib::Test::take_sample:$TAKE_SAMPLE_LINE;(unknown):Time::HiRes::usleep
);

for my $trace (@traces) {
    ok(exists $a->{flames}{$trace}, "present - $trace");
}

done_testing();
