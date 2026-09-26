sub f
{
    my ($x) = @_;
    if($x > 1)
    {
        return 1;
    }
    elsif ( $x )
    {
        return 2;
    }
    else { return 3; }
    while ($x) { $x--; }
    for (my $i = 0; $i < 3; $i++) { print $i; }
    foreach my $i (@a) { }
}



sub g { 1 }
