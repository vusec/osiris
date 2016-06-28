/* select.g */
BEG_G {
int fd = openF("nodes","r");
graph_t sg = subg($,"reach");
if ( ARGV[0] != "")
{
  sg.label = ARGV[0];
}
char* s;
node_t n;
while (scanf(fd,"%s\n",&s) == 1) {
n = node($,s);
subnode(sg,n);
}
induce(sg);
write(sg);
}

