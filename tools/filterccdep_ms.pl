#
# Filter the output from "cl.exe /showIncludes /P .." to generate a
# dependency file for GNU Make. It checks for error messages in the
# input stream and (tries) to return a non-zero exit-code in the same
# situations where clwould return a non-zero exit-code. This program
# is meant to be used when cl is called to generate an object
# file. This is unlike filterdep_ms.pl which is must be used when
# object files and dependency files are generated separately.
#
# Usage: cl .. | perl filterdep_ms.pl /v/Keepit filename.d
#
#


use strict;

$| = 1; # No buffering of stdout

my $root     = shift @ARGV or die "need root dir as first arg\n";
my $filename = shift @ARGV or die "need target filename as 2. arg\n";
my $target_path = shift @ARGV or die "need target path as 3. arg\n";

my $trace = 0;

print STDERR "$0: $root $target_path $filename\n" if $trace;

sub normalize
{
  my ($path) = @_;
  $path =~ s!\\!/!g; # \ -> /
  $path =~ s!^/(\w)/!$1:/!; # /v/xyz -> c:\xyz
  return $path;
}

$root        = normalize($root);
$filename = normalize($filename);

print "filename is \"${filename}\"\n" if $trace;

my $basename = $filename;
$basename =~ s/\.[^.]*$//; # remove .cc or .d or whatever
$basename =~ s,^src/,,; # strip src/ prefix on sources
my $depfilename = $basename.'.d';

if ($basename =~ m!^($target_path)!) {
    $basename = "$basename";
} else {
    $basename = "$target_path$basename";
}
my $depfilename = $basename.'.d';

print "basename is \"${basename}\"\n" if $trace;


my (%filenames, $error_count);
while (<STDIN>)
{
  s/
$//; # remove \r
  chomp;   # remove \n

  if (/^Note: including file:\s* (.*)\s*$/) {
    my $depfile = normalize($1);
    next if ($depfile =~ /c:.(Qt|Program)/i);
    print STDERR ">> $depfile -> " if $trace >= 2;
    $depfile =~ s!^\Q$root\E/?!!i;
    print STDERR "Depends on $depfile\n" if $trace >= 2;
    $filenames{$depfile}++;
  } else {
      print "$_\n";
      $error_count++ if (/:( fatal)? error/);
  }
}
if ($error_count) {
  print STDERR "$0: Found $error_count errors! $filename\n";
  exit 14;
}
my @dependencies = keys %filenames;
print STDERR "Depfile $depfilename\n" if $trace;
open DEPFILE, '>', $depfilename or die "open $depfilename: $!";
if (@dependencies) {
    print DEPFILE "$basename.dyn_obj $basename.st_obj $basename.d: \\\n  ";
    print DEPFILE join(" \\\n  ", sort @dependencies);
    print DEPFILE "\n\n";
}
close DEPFILE;
