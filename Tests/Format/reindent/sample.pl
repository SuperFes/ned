use strict;

sub classify {
    my ($n) = @_;
    if ($n > 0) {
        return "pos";
    }
    else {
        return "neg";
    }
}

for my $x (1 .. 3) {
    print $x;
}
