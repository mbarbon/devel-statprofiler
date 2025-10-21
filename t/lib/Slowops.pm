package t::lib::Slowops;

use strict;
use warnings;

my $LONG_STR = " X" x 1000000;

sub foo {
    -d '.' && $LONG_STR =~ s/ $// for 1..$_[0];
}

$t::lib::Test::SLOWOPS_PM = $INC{'t/lib/Slowops.pm'};

1;
