BEGIN 
{
  int border_nodes[node_t];	
  int border_edges[edge_t];
  int all_nodes[node_t];
  node_t n;
  edge_t e;
}

E 
{
  if (head.color != tail.color) 
  {
  	if ( 0 == border_edges[$])
  	{
  		border_edges[$] = 1;
  	}
  	else
  	{
  		border_edges[$]++;
  	}
	if ( 0 == border_nodes[head] )
	{
		border_nodes[head] = 1;
	}
	else
	{
		border_nodes[head]++;
	}
	if ( 0 == border_nodes[tail] )
	{
		border_nodes[tail] = 1;
	}
	else
	{
		border_nodes[tail]++;
	}
  }
  if ( 0 == all_nodes[head])
  {
  	  all_nodes[head] = 1;
  }
  else
  {
  	  all_nodes[head]++;
  }
  if ( 0 == all_nodes[tail])
  {
  	 all_nodes[tail] = 1;
  }
  else
  {
  	all_nodes[tail]++;
  }
}

END_G
{
/*
  print ("border nodes:");
  for(border_nodes[n])
  {
    print (n.name, " ", n.label, ":", n.color);
  }
  print ("inner nodes:");
  for(all_nodes[n])
  {
    if ( 0 == border_nodes[n])
    {
  		print ("deleting ", n.name, " ", n.label, ":", n.color);
  		delete(NULL, n);
  	}
  }
 */
  int colors[string];
  for (border_edges[e])
  {
    if (0 == colors[e.head.color])
    {
    	colors[e.head.color] = 1;
    }
    if (0 == colors[e.tail.color])
    {
       colors[e.tail.color] = 1;
    }
  }
  // Create new graph and create new nodes for each color.
  graph_t ng = graph("overview", "");
  ng.label = sprintf("%s%s", $.label, " [merged]");
  node_t new_nodes[string];
  string c = "";
  for (colors[c])
  {
  	new_nodes[c] = node(ng, c);
  	new_nodes[c].color = c;
  	new_nodes[c].label = "TBD";
  	new_nodes[c].style = "filled";
  }

  // For every border edge, connect the new nodes accordingly.
  edge_t new_edges[string];
  string curr_edge_name = "";
  string label_tokens[int];
  string head_name;
  string tail_name;

  for (border_edges[e])
  {
  	 if (e.head.color == "")
  	 {
  	    head_name = "common";
  	 }
  	 else
  	 {
  	 	tokens(e.head.label, label_tokens, "{_");
  	 	head_name = sprintf("%s_%s", label_tokens[0], label_tokens[1]);
  	 }
  	 if (e.tail.color == "")
  	 {
  	    tail_name = "common";
  	 }
  	 else
  	 {
		 tokens(e.tail.label, label_tokens, "{_");
		 tail_name = sprintf("%s_%s", label_tokens[0], label_tokens[1]);
	 }
     curr_edge_name = sprintf("%s->%s", head_name, tail_name);
     if (!new_edges[curr_edge_name])
     {
     	new_nodes[e.tail.color].label = tail_name;
     	new_nodes[e.head.color].label = head_name;
     	new_edges[curr_edge_name] = edge_sg(ng, new_nodes[e.tail.color], new_nodes[e.head.color], curr_edge_name);
     }
  }
  write(ng);
}
