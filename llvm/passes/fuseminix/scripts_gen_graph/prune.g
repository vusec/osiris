BEGIN {
   graph_t newg;
   node_t  n;
}
BEG_G {
   node_t del_n = node($, ARGV[0]);
   delete($,del_n);
}
END_G {
	$O = $;
}
