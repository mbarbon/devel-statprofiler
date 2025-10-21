#!/usr/bin/env perl

use t::lib::Test;

use Devel::StatProfiler::Report;

my $profile_file;
BEGIN { $profile_file = temp_profile_file(); }

use Devel::StatProfiler -file => $profile_file, -interval => 1000;
my ($foo, $l1, $l2, $l3);

BEGIN { Time::HiRes::sleep(0.05); }

{
    package X;

    $foo = sub {
        main::take_sample(); BEGIN { $l1 = __LINE__ + 0 }
    };
}

sub Moo::bar {
    take_sample(); BEGIN { $l2 = __LINE__ + 0 }
}

sub foo {
    take_sample(); BEGIN { $l3 = __LINE__ + 0 }
}

foo();
Moo::bar();
$foo->();

Devel::StatProfiler::stop_profile();

my $take_sample_line = $t::lib::Test::TAKE_SAMPLE_LINE;
my $r = Devel::StatProfiler::Report->new(flamegraph => 1);
my $a = $r->{aggregate};
$r->add_trace_file($profile_file);

# sanity checking
eq_or_diff([sort grep +(m{t/lib/Test\.pm$} || (!m{^/} && !m{xs:(?!Time/HiRes.pm)})), keys %{$a->{files}}],
           # need to sort this because of / vs. \ in __FILE__
           [sort 'xs:Time/HiRes.pm', __FILE__, $TEST_PM]);

my $total; $total += $_->{exclusive} for values %{$a->{files}};
is($a->{total}, $total);

### start setup

# Time::HiRes
my $time_hires = $a->{files}{'xs:Time/HiRes.pm'};
my ($usleep) = grep $_->{name} eq 'Time::HiRes::usleep',
               map  $a->{subs}{$_},
                    keys %{$time_hires->{subs}{-1}};

# current file
my $me = $a->{files}{__FILE__ . ''};
my $moo = sub_at_line($a, $me, $l2);

# t/lib/Test.pm
my $test_pm = $a->{files}{$TEST_PM};
my $take_sample = sub_at_line($a, $test_pm, $take_sample_line);

### end setup
### start all subroutines

# subroutine map
is($a->{subs}{__FILE__ . ':Moo::bar:24'}, $moo);
is($a->{subs}{'(unknown):Time::HiRes::usleep'}, $usleep);
is($a->{subs}{"$TEST_PM:t::lib::Test::take_sample:" . $take_sample_line}, $take_sample);

### end all subroutines
### start subroutine attributes

# Time::HiRes basic attributes
is($usleep->{name}, 'Time::HiRes::usleep');
is($usleep->{package}, 'Time::HiRes');
is($usleep->{start_line}, -1);
is($usleep->{kind}, 1);
is($usleep->{file}, 'xs:Time/HiRes.pm');
cmp_ok($usleep->{exclusive}, '>=', 10 / precision_factor);
cmp_ok($usleep->{inclusive}, '==', $usleep->{exclusive});

# the only usleep call site is from take_sample
eq_or_diff([sort keys %{$usleep->{call_sites}}], ["$TEST_PM:$take_sample_line"]);
{
    my $cs = $usleep->{call_sites}{"$TEST_PM:$take_sample_line"};
    is($cs->{caller}, $take_sample->{uq_name});
    is($cs->{exclusive}, $usleep->{exclusive});
    is($cs->{inclusive}, $usleep->{inclusive});
    is($cs->{file}, $TEST_PM);
    is($cs->{line}, $take_sample_line);
}

# take_sample basic attributes
is($take_sample->{name}, 't::lib::Test::take_sample');
is($take_sample->{package}, 't::lib::Test');
is($take_sample->{start_line}, $take_sample_line);
is($take_sample->{kind}, 0);
is($take_sample->{file}, $TEST_PM);
cmp_ok($take_sample->{exclusive}, '<', 10 / precision_factor);
cmp_ok($take_sample->{inclusive}, '>=', 10 / precision_factor);

# three call sites for take_sample
eq_or_diff([sort keys %{$take_sample->{call_sites}}],
           [map __FILE__ . ':' . $_, ($l1, $l2, $l3)]);

# Moo::bar call site for take_sample
{
    my $cs = $take_sample->{call_sites}{__FILE__ . ':' . $l2};
    is($cs->{caller}, $moo->{uq_name});
    cmp_ok($cs->{exclusive}, '<=', 5 / precision_factor);
    cmp_ok($cs->{inclusive}, '>=', 5 / precision_factor);
    is($cs->{file}, __FILE__);
    is($cs->{line}, $l2);
}

### end subroutine attributes
### start file attributes

# t/lib/Test.pm
is($test_pm->{name}, $TEST_PM);
like($test_pm->{report}, qr/Test-pm-[a-z0-9]{20}-line.html/);
cmp_ok($test_pm->{exclusive}, '<=', 5 / precision_factor);
# WTF cmp_ok($test_pm->{inclusive}, '>=', 20);
cmp_ok($test_pm->{lines}{inclusive}[$take_sample_line], '>=', 10 / precision_factor);

#subs
is(sub_at_line($a, $test_pm, $take_sample_line), $take_sample);

### end file attributes
### start flamegraph

my @traces = map {
    s{t/lib/Test\.pm}{$TEST_PM}reg =~
    s{__FILE__}{__FILE__}reg =~
    s{\$TAKE_SAMPLE_LINE}{$t::lib::Test::TAKE_SAMPLE_LINE}rg
} qw(
    __FILE__:main;__FILE__:main::foo:28;t/lib/Test.pm:t::lib::Test::take_sample:$TAKE_SAMPLE_LINE;(unknown):Time::HiRes::usleep
    __FILE__:main;__FILE__:X::__ANON__:19;t/lib/Test.pm:t::lib::Test::take_sample:$TAKE_SAMPLE_LINE;(unknown):Time::HiRes::usleep
    __FILE__:main::BEGIN:13;(unknown):Time::HiRes::sleep
    __FILE__:main;__FILE__:Moo::bar:24;t/lib/Test.pm:t::lib::Test::take_sample:$TAKE_SAMPLE_LINE;(unknown):Time::HiRes::usleep
);

for my $trace (@traces) {
    ok(exists $a->{flames}{$trace}, "present - $trace");
}

### end flamegraph

done_testing();
